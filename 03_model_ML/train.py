from __future__ import annotations

import io
import time
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.compose import ColumnTransformer
from sklearn.ensemble import HistGradientBoostingRegressor, RandomForestRegressor
from sklearn.linear_model import Ridge
from sklearn.metrics import mean_absolute_error, r2_score
from sklearn.model_selection import train_test_split
from sklearn.pipeline import Pipeline
from sklearn.preprocessing import OneHotEncoder, StandardScaler

DATASET_NAME = "dataset_sintetico_plantas_numerico.csv"
FEATURE_COLUMNS = ["localname", "moisture", "temperature"]
TARGET_COLUMN = "mililitros_riego"
CATEGORICAL_COLUMNS = ["localname"]
NUMERIC_COLUMNS = ["moisture", "temperature"]
TEST_SIZE = 0.2
RANDOM_STATE = 42
LATENCY_WARMUP = 20
LATENCY_REPEATS = 500
COMPARISON_CSV = "model_comparison.csv"


def load_dataset(csv_path: Path) -> pd.DataFrame:
    df = pd.read_csv(csv_path)
    required = FEATURE_COLUMNS + [TARGET_COLUMN]
    missing = [col for col in required if col not in df.columns]
    if missing:
        raise ValueError(f"Dataset is missing required columns: {missing}")
    df = df.dropna(subset=required).copy()
    df["localname"] = df["localname"].astype(int)
    return df


def build_preprocessor() -> ColumnTransformer:
    """One-hot plant id; scale moisture and temperature for all models."""
    return ColumnTransformer(
        transformers=[
            (
                "cat",
                OneHotEncoder(handle_unknown="ignore", sparse_output=False),
                CATEGORICAL_COLUMNS,
            ),
            ("num", StandardScaler(), NUMERIC_COLUMNS),
        ]
    )


def build_candidates() -> dict[str, Pipeline]:
    """Three candidates: fast linear baseline, original RF, boosted trees."""
    return {
        "ridge": Pipeline(
            steps=[
                ("preprocess", build_preprocessor()),
                ("model", Ridge(alpha=1.0)),
            ]
        ),
        "random_forest": Pipeline(
            steps=[
                ("preprocess", build_preprocessor()),
                (
                    "model",
                    RandomForestRegressor(
                        n_estimators=100,
                        random_state=RANDOM_STATE,
                        n_jobs=1,
                    ),
                ),
            ]
        ),
        "hist_gradient_boosting": Pipeline(
            steps=[
                ("preprocess", build_preprocessor()),
                (
                    "model",
                    HistGradientBoostingRegressor(
                        max_depth=6,
                        learning_rate=0.1,
                        max_iter=200,
                        random_state=RANDOM_STATE,
                    ),
                ),
            ]
        ),
    }


def measure_batch_latency_ms(pipeline: Pipeline, X: pd.DataFrame) -> float:
    for _ in range(3):
        pipeline.predict(X)
    start = time.perf_counter()
    pipeline.predict(X)
    elapsed = time.perf_counter() - start
    return elapsed * 1000.0


def measure_single_sample_latency_ms(pipeline: Pipeline, X_one_row: pd.DataFrame) -> float:
    for _ in range(LATENCY_WARMUP):
        pipeline.predict(X_one_row)
    start = time.perf_counter()
    for _ in range(LATENCY_REPEATS):
        pipeline.predict(X_one_row)
    elapsed = time.perf_counter() - start
    return (elapsed / LATENCY_REPEATS) * 1000.0


def serialized_size_kb(obj) -> float:
    """Uncompressed pickle size in KiB. Proxy for RAM needed to hold the model."""
    buf = io.BytesIO()
    joblib.dump(obj, buf, compress=0)
    return buf.getbuffer().nbytes / 1024.0


def estimator_footprint(pipeline: Pipeline) -> tuple[int, str]:
    """MCU-oriented complexity: coefficients or tree nodes."""
    model = pipeline.named_steps["model"]
    if isinstance(model, Ridge):
        n_params = int(np.size(model.coef_) + np.size(model.intercept_))
        return n_params, "coefficients"
    if isinstance(model, RandomForestRegressor):
        n_nodes = int(sum(tree.tree_.node_count for tree in model.estimators_))
        return n_nodes, "tree_nodes"
    if isinstance(model, HistGradientBoostingRegressor):
        n_nodes = 0
        for iteration in getattr(model, "_predictors", []):
            for tree in iteration:
                nodes = getattr(tree, "nodes", None)
                n_nodes += len(nodes) if nodes is not None else 1
        return int(n_nodes), "tree_nodes"
    return 0, "unknown"


def evaluate_model(
    name: str,
    pipeline: Pipeline,
    X_train: pd.DataFrame,
    y_train: pd.Series,
    X_test: pd.DataFrame,
    y_test: pd.Series,
) -> tuple[dict, Pipeline, np.ndarray]:
    fit_start = time.perf_counter()
    pipeline.fit(X_train, y_train)
    train_time_s = time.perf_counter() - fit_start

    y_pred = pipeline.predict(X_test)
    mae = mean_absolute_error(y_test, y_pred)
    r2 = r2_score(y_test, y_pred)
    batch_ms = measure_batch_latency_ms(pipeline, X_test)
    per_sample_ms = measure_single_sample_latency_ms(pipeline, X_test.iloc[[0]])
    n_units, unit_kind = estimator_footprint(pipeline)

    metrics = {
        "model": name,
        "mae_ml": float(mae),
        "r2": float(r2),
        "train_time_s": train_time_s,
        "batch_predict_ms": batch_ms,
        "single_sample_ms": per_sample_ms,
        "pipeline_kb": serialized_size_kb(pipeline),
        "estimator_kb": serialized_size_kb(pipeline.named_steps["model"]),
        "n_units": n_units,
        "unit_kind": unit_kind,
        "n_test": int(len(y_test)),
    }
    return metrics, pipeline, y_pred


def _best_by_r2(results: pd.DataFrame) -> str:
    """Highest R2; if tied, the lower MAE wins."""
    r2_max = results["r2"].max()
    tied = results[np.isclose(results["r2"], r2_max, atol=1e-8)]
    return tied.loc[tied["mae_ml"].idxmin(), "model"]


def print_comparison(results: pd.DataFrame) -> None:
    print("\n=== Model comparison (test set) ===")
    print(
        results.to_string(
            index=False,
            formatters={
                "mae_ml": "{:.4f}".format,
                "r2": "{:.6f}".format,
                "train_time_s": "{:.4f}".format,
                "batch_predict_ms": "{:.4f}".format,
                "single_sample_ms": "{:.4f}".format,
                "pipeline_kb": "{:.2f}".format,
                "estimator_kb": "{:.2f}".format,
            },
        )
    )

    best_mae = results.loc[results["mae_ml"].idxmin(), "model"]
    best_r2 = _best_by_r2(results)
    fastest_infer = results.loc[results["single_sample_ms"].idxmin(), "model"]
    fastest_train = results.loc[results["train_time_s"].idxmin(), "model"]
    smallest = results.loc[results["estimator_kb"].idxmin(), "model"]

    print("\n=== Quick ranking ===")
    print(f"Lowest MAE:              {best_mae}")
    print(f"Highest R2:              {best_r2}")
    print(f"Fastest inference:       {fastest_infer}")
    print(f"Fastest training:        {fastest_train}")
    print(f"Smallest for edge/MCU:   {smallest}")
    print(
        "\nThis dataset is synthetic and mostly rule-based, so tree models "
        "can reach R2 ~ 1.0. On real pot data the gap will be smaller."
    )
    print(
        "Pick the model that fits your deployment: "
        "prefer lowest MAE / highest R2 for accuracy, "
        "lowest single_sample_ms for speed, "
        "lowest estimator_kb / n_units for microcontroller flash and RAM."
    )
    print(
        "Note: sklearn pickles are not flashed as-is on an ESP32. "
        "estimator_kb and n_units are relative cost; TinyML would still "
        "need a compact export (e.g. TFLite Micro or a C array of trees)."
    )


def main() -> None:
    base_dir = Path(__file__).resolve().parent
    dataset_path = base_dir / DATASET_NAME
    df = load_dataset(dataset_path)

    X = df[FEATURE_COLUMNS]
    y = df[TARGET_COLUMN]

    X_train, X_test, y_train, y_test = train_test_split(
        X,
        y,
        test_size=TEST_SIZE,
        random_state=RANDOM_STATE,
        stratify=X["localname"],
    )

    print(f"Rows: {len(df)} | train: {len(X_train)} | test: {len(X_test)}")
    print(f"Features: {FEATURE_COLUMNS} -> target: {TARGET_COLUMN}")

    candidates = build_candidates()

    rows = []
    fitted = {}
    for name, pipeline in candidates.items():
        print(f"\nTraining {name}...")
        metrics, fitted_pipeline, _ = evaluate_model(
            name, pipeline, X_train, y_train, X_test, y_test
        )
        rows.append(metrics)
        fitted[name] = fitted_pipeline
        model_path = base_dir / f"model_{name}.pkl"
        joblib.dump(fitted_pipeline, model_path)
        print(
            f"  MAE={metrics['mae_ml']:.4f} ml | "
            f"R2={metrics['r2']:.4f} | "
            f"train={metrics['train_time_s']:.4f}s | "
            f"1-sample={metrics['single_sample_ms']:.4f} ms | "
            f"estimator={metrics['estimator_kb']:.2f} KiB "
            f"({metrics['n_units']} {metrics['unit_kind']})"
        )
        print(f"  Saved {model_path.name}")

    results = pd.DataFrame(rows)
    comparison_path = base_dir / COMPARISON_CSV
    results.to_csv(comparison_path, index=False)
    print_comparison(results)
    print(f"\nComparison table saved as '{comparison_path.name}'")


if __name__ == "__main__":
    main()


"""
-- Script para verificar y agregar especies que reconoce el modelo

-- Verificar especies existentes
SELECT * FROM species;

-- Insertar las especies que reconoce el modelo de TensorFlow
-- (Solo si no existen)

INSERT IGNORE INTO species (common_name, scientific_name, optimal_water_frequency_days, optimal_light_hours, optimal_humidity_percent) 
VALUES 
('ajo', 'Allium sativum', 3, 6, 60),
('geranio', 'Pelargonium', 4, 6, 50),
('hierbabuena', 'Mentha spicata', 2, 5, 70),
('menta', 'Mentha', 2, 5, 70),
('oregano', 'Origanum vulgare', 5, 6, 40),
('orquidea', 'Orchidaceae', 7, 4, 60),
('rosachina', 'Hibiscus rosa-sinensis', 3, 6, 60),
('tomatecherry', 'Solanum lycopersicum var. cerasiforme', 2, 8, 65);

-- Verificar inserción
SELECT id, common_name, scientific_name FROM species;

"""
