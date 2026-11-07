"""Empaqueta las dos Lambdas de PotAI listas para subir a S3.

    python build_backend_zip.py

Genera (solo con la libreria estandar, sin instalar nada):
    dist/ingest.zip   <- lambda_ingest/lambda_function.py
    dist/api.zip      <- lambda_api/lambda_function.py + lambda_api/plants.py

Los archivos quedan en la RAIZ del zip, que es lo que espera el handler
`lambda_function.lambda_handler`. boto3 ya viene incluido en AWS Lambda.
"""

import py_compile
import sys
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIST = HERE / "dist"

PACKAGES = {
    "ingest.zip": [HERE / "lambda_ingest" / "lambda_function.py"],
    "api.zip": [
        HERE / "lambda_api" / "lambda_function.py",
        HERE / "lambda_api" / "plants.py",
    ],
}

# Fecha fija: el mismo codigo siempre produce el mismo zip.
FIXED_DATE = (2026, 1, 1, 0, 0, 0)


def main() -> int:
    DIST.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for zip_name, sources in PACKAGES.items():
            for src in sources:
                if not src.exists():
                    print(f"ERROR: falta {src}")
                    return 1
                py_compile.compile(str(src), cfile=str(Path(tmp) / (src.name + "c")), doraise=True)

            target = DIST / zip_name
            with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zf:
                for src in sources:
                    info = zipfile.ZipInfo(src.name, FIXED_DATE)
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o644 << 16
                    zf.writestr(info, src.read_bytes())
            print(f"OK  {target}  ({target.stat().st_size} bytes): {', '.join(s.name for s in sources)}")

    print("\nSiguiente paso: suba ambos .zip al bucket S3 dentro de la carpeta potai/ (ver POTAI_BACKEND.md).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
