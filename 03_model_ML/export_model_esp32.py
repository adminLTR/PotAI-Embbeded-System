"""
Exporta model_random_forest.pkl (sklearn Pipeline) a un binario compacto
para la ESP32 (edge computing).

Uso (desde 03_model_ML, con el venv activo):

    python export_model_esp32.py
    python export_model_esp32.py --n-trees 30        # modelo aun mas chico
    python export_model_esp32.py --model model_random_forest.pkl --out otra/ruta.bin

Que hace (compresion):
  1. Descarta el Pipeline de sklearn (OneHotEncoder + StandardScaler + RF).
     Solo se conservan los arboles.
  2. Reescribe cada umbral al dominio ORIGINAL de las entradas
     (planta, humedad suelo %, temperatura C), eliminando el escalado y el
     one-hot. La ESP32 no necesita ninguna transformacion previa.
  3. Cuantiza: entradas en decimas (x10, int16) y hojas en Q8.8 (uint16).
  4. Guarda cada nodo en 4 bytes (meta uint16 + payload uint16) en preorden
     (hijo izquierdo = nodo siguiente, solo se guarda el indice del derecho).
  5. Verifica el binario contra el pipeline original antes de terminar.

Formato del binario (little endian):
    offset  tam  campo
    0       4    magic "A10M"
    4       1    version (1)
    5       1    reservado
    6       2    n_trees
    8       2    n_nodes
    10      2    leaf_scale   (ml = payload / leaf_scale)
    12      2    input_scale  (entrada_int = valor * input_scale)
    14      2    crc16-ccitt del cuerpo (todo lo que sigue al header)
    16      2*T  roots[T]     indice global del nodo raiz de cada arbol
    ...     2*N  meta[N]      bits 15..14 = tipo, bits 13..0 = indice hijo derecho
    ...     2*N  payload[N]   nodo interno: umbral int16 | hoja: ml*leaf_scale uint16

    tipo: 0 = humedad suelo <= umbral ? izq : der
          1 = temperatura   <= umbral ? izq : der
          2 = planta == payload ? der : izq
          3 = hoja
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

import joblib
import numpy as np
import pandas as pd

HERE = Path(__file__).resolve().parent
DEFAULT_MODEL = HERE / "model_random_forest.pkl"
DEFAULT_OUT = (
    HERE.parent / "01_firmware_esp32" / "components" / "ml_model" / "model" / "rf_model.bin"
)
DATASET = HERE / "dataset_sintetico_plantas_numerico.csv"

MAGIC = b"A10M"
VERSION = 1
INPUT_SCALE = 10      # 0.1 de resolucion para humedad y temperatura
LEAF_SCALE = 256      # Q8.8 -> hasta 255.99 ml
KIND_MOISTURE, KIND_TEMP, KIND_PLANT, KIND_LEAF = 0, 1, 2, 3
MAX_NODES = (1 << 14) - 1


def crc16_ccitt(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def feature_map(pipeline) -> list[tuple[int, int]]:
    """Para cada columna transformada devuelve (kind, valor).

    kind PLANT -> valor = id de planta (one-hot)
    kind MOISTURE/TEMP -> valor = indice en StandardScaler
    """
    pre = pipeline.named_steps["preprocess"]
    names = list(pre.get_feature_names_out())
    cat_enc = pre.named_transformers_["cat"]
    categories = [int(c) for c in cat_enc.categories_[0]]
    num_cols = list(pre.transformers_[1][2])
    mapping: list[tuple[int, int]] = []
    plant_idx = 0
    for name in names:
        if name.startswith("cat__"):
            mapping.append((KIND_PLANT, categories[plant_idx]))
            plant_idx += 1
        elif name == "num__moisture":
            mapping.append((KIND_MOISTURE, num_cols.index("moisture")))
        elif name == "num__temperature":
            mapping.append((KIND_TEMP, num_cols.index("temperature")))
        else:
            raise ValueError(f"Feature no soportada: {name}")
    return mapping


def encode_tree(tree, fmap, scaler, nodes: list[tuple[int, int]], leaf_max: list[float]) -> int:
    """Serializa un arbol en preorden. Devuelve el indice global de su raiz."""
    t = tree.tree_
    root = len(nodes)

    def rec(node_id: int) -> None:
        idx = len(nodes)
        left, right = t.children_left[node_id], t.children_right[node_id]
        if left == -1:  # hoja
            val = float(t.value[node_id].ravel()[0])
            leaf_max.append(val)
            q = int(round(val * LEAF_SCALE))
            if not 0 <= q <= 0xFFFF:
                raise ValueError(f"Hoja fuera de rango Q8.8: {val}")
            nodes.append((KIND_LEAF << 14, q))
            return

        kind, val = fmap[int(t.feature[node_id])]
        thr = float(t.threshold[node_id])
        nodes.append((0, 0))  # placeholder, se completa tras recorrer el hijo izquierdo
        if kind == KIND_PLANT:
            if not 0.0 <= thr <= 1.0:
                raise ValueError("Umbral one-hot inesperado")
            payload = val  # id de planta; der = planta == id
            # one-hot <= thr (izq) => planta distinta ; > thr (der) => planta == id
        else:
            raw = thr * scaler.scale_[val] + scaler.mean_[val]
            # Los umbrales de sklearn son puntos medios entre muestras de
            # entrenamiento (p.ej. 39.5). Con entradas en rejilla de 0.1,
            # "x <= thr" equivale a "x10 <= floor(thr*10)". Solo el empate
            # exacto (x == thr) puede diferir por ruido de float32 de sklearn.
            payload = int(np.floor(raw * INPUT_SCALE))
            if not -32768 <= payload <= 32767:
                raise ValueError("Umbral fuera de int16")
        rec(int(left))
        right_idx = len(nodes)
        if right_idx > MAX_NODES:
            raise ValueError("Demasiados nodos (>16383)")
        rec(int(right))
        nodes[idx] = ((kind << 14) | right_idx, payload & 0xFFFF)

    rec(0)
    return root


def build_binary(pipeline, n_trees: int | None) -> tuple[bytes, dict]:
    model = pipeline.named_steps["model"]
    scaler = pipeline.named_steps["preprocess"].named_transformers_["num"]
    fmap = feature_map(pipeline)
    trees = list(model.estimators_)
    if n_trees is not None:
        trees = trees[:n_trees]
    if not trees:
        raise ValueError("Sin arboles")

    nodes: list[tuple[int, int]] = []
    roots: list[int] = []
    leaf_vals: list[float] = []
    for tr in trees:
        roots.append(encode_tree(tr, fmap, scaler, nodes, leaf_vals))

    body = b"".join(struct.pack("<H", r) for r in roots)
    body += b"".join(struct.pack("<H", m) for m, _ in nodes)
    body += b"".join(struct.pack("<H", p) for _, p in nodes)
    header = (
        MAGIC
        + struct.pack("<BBHHHHH", VERSION, 0, len(trees), len(nodes), LEAF_SCALE, INPUT_SCALE, crc16_ccitt(body))
    )
    info = {"n_trees": len(trees), "n_nodes": len(nodes), "leaf_max": max(leaf_vals)}
    return header + body, info


class BinaryModel:
    """Misma logica que components/ml_model/ml_model.c (para verificar)."""

    def __init__(self, blob: bytes):
        assert blob[:4] == MAGIC
        _, _, self.t, self.n, self.leaf_scale, self.input_scale, crc = struct.unpack("<BBHHHHH", blob[4:16])
        body = blob[16:]
        assert crc16_ccitt(body) == crc, "CRC invalido"
        u16 = np.frombuffer(body, dtype="<u2")
        self.roots = u16[: self.t].astype(int)
        self.meta = u16[self.t : self.t + self.n].astype(int)
        raw = u16[self.t + self.n : self.t + 2 * self.n]
        self.payload = raw.astype(int)
        self.payload_s = raw.astype(np.int16).astype(int)

    def predict(self, plant: int, moisture: float, temperature: float) -> float:
        m10 = int(round(moisture * self.input_scale))
        t10 = int(round(temperature * self.input_scale))
        total = 0
        for r in self.roots:
            i = int(r)
            while True:
                kind = self.meta[i] >> 14
                if kind == KIND_LEAF:
                    total += int(self.payload[i])
                    break
                right = self.meta[i] & 0x3FFF
                if kind == KIND_MOISTURE:
                    i = i + 1 if m10 <= self.payload_s[i] else right
                elif kind == KIND_TEMP:
                    i = i + 1 if t10 <= self.payload_s[i] else right
                else:
                    i = right if plant == self.payload[i] else i + 1
        return total / (self.t * self.leaf_scale)


def verify(pipeline, blob: bytes) -> None:
    bm = BinaryModel(blob)
    plants = [int(p) for p in pipeline.named_steps["preprocess"].named_transformers_["cat"].categories_[0]]
    rows = [(p, m / 1.0, t / 10.0) for p in plants for m in range(0, 101, 2) for t in range(100, 451, 10)]
    df = pd.DataFrame(rows, columns=["localname", "moisture", "temperature"])
    ref = pipeline.predict(df)
    got = np.array([bm.predict(*r) for r in rows])
    err = np.abs(ref - got)

    if DATASET.exists():
        d = pd.read_csv(DATASET)
        ref_d = pipeline.predict(d[["localname", "moisture", "temperature"]])
        got_d = np.array([bm.predict(int(a), float(b), float(c)) for a, b, c in d[["localname", "moisture", "temperature"]].to_numpy()])
        err_d = np.abs(ref_d - got_d)
        mae_real = np.abs(d["mililitros_riego"].to_numpy() - got_d).mean()
        print(f"  Dataset ({len(d)} filas): max|dif| vs pkl = {err_d.max():.4f} ml | MAE binario vs real = {mae_real:.4f} ml")
    print(f"  Rejilla  ({len(rows)} puntos): max|dif| vs pkl = {err.max():.4f} ml | media = {err.mean():.5f} ml")
    if err.max() > 1.0:
        raise SystemExit("ERROR: el binario difiere demasiado del modelo original")

    print("  Vectores de prueba (planta, humedad suelo %, temp C -> ml):")
    for p, m, t in [(1, 28, 30), (1, 70, 25), (3, 15, 22), (8, 40, 33)]:
        print(f"    ({p}, {m}, {t}) -> {bm.predict(p, m, t):.1f} ml (pkl: {float(pipeline.predict(pd.DataFrame([[p, m, t]], columns=['localname', 'moisture', 'temperature']))[0]):.1f})")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--n-trees", type=int, default=None, help="usar solo los primeros N arboles")
    args = ap.parse_args()

    print(f"Cargando {args.model} ...")
    pipeline = joblib.load(args.model)
    blob, info = build_binary(pipeline, args.n_trees)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)

    pkl_kb = args.model.stat().st_size / 1024
    print(f"Arboles: {info['n_trees']} | nodos: {info['n_nodes']} | hoja max: {info['leaf_max']:.1f} ml")
    print(f"Tamano: {args.model.name} = {pkl_kb:.1f} KiB  ->  {args.out.name} = {len(blob)} bytes "
          f"({len(blob) / 1024:.2f} KiB, {pkl_kb * 1024 / len(blob):.0f}x menor)")
    print(f"Escrito en: {args.out}")
    print("Verificando contra el pipeline original ...")
    verify(pipeline, blob)
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
