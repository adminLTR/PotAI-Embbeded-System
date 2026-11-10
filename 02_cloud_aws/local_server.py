"""Servidor local de PotAI: la misma API que AWS, sin cuenta ni Docker obligatorio.

No sustituye IoT Core. Sirve para que la app (y el telefono) hablen con
las Lambdas reales usando DynamoDB en memoria.

    python local_server.py
    docker compose up --build

En PotAI → Ajustes (modo demo OFF):

    PC / Chrome     http://127.0.0.1:8080
    Emulador Android http://10.0.2.2:8080
    Telefono (WiFi) http://<IP-de-tu-PC>:8080
    Thing Name      psi3542-g01
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import sys
import threading
import time
import types
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parent


# --------------------------------------------------------------------------- DynamoDB / IoT falsos (igual que tests/)


class FakeClientError(Exception):
    def __init__(self, code):
        super().__init__(code)
        self.response = {"Error": {"Code": code}}


class _Cond:
    def __init__(self, checks):
        self.checks = checks

    def __and__(self, other):
        return _Cond(self.checks + other.checks)


class FakeKey:
    def __init__(self, name):
        self.name = name

    def eq(self, value):
        return _Cond([("eq", self.name, value)])

    def gte(self, value):
        return _Cond([("gte", self.name, value)])


class FakeTable:
    def __init__(self):
        self.items = []
        self._lock = threading.Lock()

    def put_item(self, Item, ConditionExpression=None):
        with self._lock:
            for it in self.items:
                if (
                    it["thingName"] == Item["thingName"]
                    and it["ingestionKey"] == Item["ingestionKey"]
                ):
                    raise FakeClientError("ConditionalCheckFailedException")
            self.items.append(Item)

    def query(
        self,
        KeyConditionExpression,
        ScanIndexForward=True,
        Limit=None,
        ConsistentRead=False,
    ):
        with self._lock:
            rows = list(self.items)
        for op, name, value in KeyConditionExpression.checks:
            rows = [
                r
                for r in rows
                if (r[name] == value if op == "eq" else r[name] >= value)
            ]
        rows = sorted(rows, key=lambda r: r["ingestionKey"], reverse=not ScanIndexForward)
        return {"Items": rows[:Limit] if Limit else rows}


class FakeIot:
    def __init__(self):
        self.published = []
        self._lock = threading.Lock()

    def publish(self, **kwargs):
        with self._lock:
            self.published.append(kwargs)


def _install_fake_boto3():
    boto3 = types.ModuleType("boto3")
    boto3.resource = lambda *a, **k: None
    boto3.client = lambda *a, **k: None
    conditions = types.ModuleType("boto3.dynamodb.conditions")
    conditions.Key = FakeKey
    dynamodb = types.ModuleType("boto3.dynamodb")
    dynamodb.conditions = conditions
    boto3.dynamodb = dynamodb
    botocore = types.ModuleType("botocore")
    exceptions = types.ModuleType("botocore.exceptions")
    exceptions.ClientError = FakeClientError
    botocore.exceptions = exceptions
    sys.modules.update(
        {
            "boto3": boto3,
            "boto3.dynamodb": dynamodb,
            "boto3.dynamodb.conditions": conditions,
            "botocore": botocore,
            "botocore.exceptions": exceptions,
        }
    )


def _load(name, path, extra_path=None):
    if extra_path:
        sys.path.insert(0, str(extra_path))
    try:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        if extra_path:
            sys.path.remove(str(extra_path))


# --------------------------------------------------------------------------- runtime


class Runtime:
    def __init__(self):
        _install_fake_boto3()
        self.ingest = _load(
            "potai_ingest_local", ROOT / "lambda_ingest" / "lambda_function.py"
        )
        self.api = _load(
            "potai_api_local",
            ROOT / "lambda_api" / "lambda_function.py",
            ROOT / "lambda_api",
        )
        self.telemetry = FakeTable()
        self.irrigation = FakeTable()
        self.iot = FakeIot()
        self.ingest._cache.update(
            {
                "PSI3542Telemetry": self.telemetry,
                "PSI3542Irrigation": self.irrigation,
            }
        )
        self.api._cache.update(
            {
                "telemetry": self.telemetry,
                "irrigation": self.irrigation,
                "iot": self.iot,
            }
        )
        self.seq = 0
        self._seq_lock = threading.Lock()

    def next_seq(self):
        with self._seq_lock:
            self.seq += 1
            return self.seq


DEVICE_PATH = re.compile(
    r"^/devices/(?P<thing>[^/]+)/(?P<rest>dashboard|telemetry/latest|"
    r"telemetry/history|irrigations|command)$"
)

ROUTES = {
    "dashboard": "GET /devices/{thingName}/dashboard",
    "telemetry/latest": "GET /devices/{thingName}/telemetry/latest",
    "telemetry/history": "GET /devices/{thingName}/telemetry/history",
    "irrigations": "GET /devices/{thingName}/irrigations",
    "command": "POST /devices/{thingName}/command",
}

CORS = {
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "content-type",
}


def handle(runtime: Runtime, method: str, path: str, query=None, body=None):
    """Devuelve (status, headers, body_dict_or_None)."""
    method = method.upper()
    path = path.rstrip("/") or "/"
    query = query or {}
    headers = {"Content-Type": "application/json", **CORS}

    if method == "OPTIONS":
        return 204, headers, None

    if method == "GET" and path == "/health":
        return 200, headers, {"ok": True, "mode": "local"}

    if method == "GET" and path == "/plants":
        result = runtime.api.lambda_handler({"routeKey": "GET /plants"}, None)
        return result["statusCode"], headers, json.loads(result["body"])

    if method == "POST" and path == "/ingest":
        try:
            payload = json.loads(body or "")
        except (TypeError, json.JSONDecodeError):
            return 400, headers, {"error": "invalid_json"}
        if not isinstance(payload, dict):
            return 400, headers, {"error": "invalid_json"}
        payload = dict(payload)
        payload.setdefault("device_id", payload.get("thingName", "psi3542-g01"))
        payload.setdefault("thingName", payload["device_id"])
        payload.setdefault("ingested_at_ms", int(time.time() * 1000))
        payload.setdefault("message_trace_id", uuid.uuid4().hex)
        if "seq" not in payload:
            payload["seq"] = runtime.next_seq()
        out = runtime.ingest.lambda_handler(payload, None)
        status = 200 if out.get("status") in ("stored", "duplicate") else 400
        return status, headers, out

    match = DEVICE_PATH.match(path)
    if not match:
        return 404, headers, {"error": "route_not_found"}

    rest = match.group("rest")
    thing = match.group("thing")
    expected = ROUTES[rest]
    if method != expected.split()[0]:
        return 405, headers, {"error": "method_not_allowed"}

    event = {
        "routeKey": expected,
        "pathParameters": {"thingName": thing},
        "queryStringParameters": {k: v[-1] for k, v in query.items()} if query else None,
    }
    if method == "POST":
        event["body"] = body if isinstance(body, str) else json.dumps(body)
    result = runtime.api.lambda_handler(event, None)
    return result["statusCode"], headers, json.loads(result["body"])


def seed(runtime: Runtime, thing="psi3542-g01"):
    now = int(time.time() * 1000)
    for i in range(40, 0, -1):
        ms = now - (i - 1) * 30_000
        soil = 25 + (i % 40)
        runtime.ingest.lambda_handler(
            {
                "thingName": thing,
                "device_id": thing,
                "seq": i,
                "temperature_c": 24.0 + (i % 5) * 0.4,
                "humidity_pct": 55.0,
                "valid": True,
                "sample_uptime_ms": i * 30_000,
                "ingested_at_ms": ms,
                "message_trace_id": f"seed-t-{i}",
                "soil_moisture_pct": float(soil),
                "soil_valid": True,
                "plant_id": 4,
                "ml_predicted": 120.0 if soil < 40 else 0.0,
                "pump_on": False,
                "irrigation_count": 3,
            },
            None,
        )
    for i, hours_ago in enumerate((18, 8, 2), start=1):
        runtime.ingest.lambda_handler(
            {
                "record_type": "irrigation",
                "thingName": thing,
                "device_id": thing,
                "seq": i,
                "plant_id": 4,
                "soil_moisture_pct": 28.0,
                "temperature_c": 25.5,
                "ml_predicted": 120.0,
                "ml_delivered": 118.0 - i,
                "duration_ms": 5900,
                "pump_uptime_ms": 100000 + i,
                "ingested_at_ms": now - hours_ago * 3600_000,
                "message_trace_id": f"seed-i-{i}",
            },
            None,
        )
    runtime.seq = 40


def live_loop(runtime: Runtime, thing: str, interval_s: float, stop: threading.Event):
    while not stop.wait(interval_s):
        seq = runtime.next_seq()
        now = int(time.time() * 1000)
        soil = 30 + (seq % 35)
        runtime.ingest.lambda_handler(
            {
                "thingName": thing,
                "device_id": thing,
                "seq": seq,
                "temperature_c": 24.5,
                "humidity_pct": 57.0,
                "valid": True,
                "sample_uptime_ms": seq * 5000,
                "ingested_at_ms": now,
                "message_trace_id": f"live-{seq}",
                "soil_moisture_pct": float(soil),
                "soil_valid": True,
                "plant_id": 4,
                "ml_predicted": 120.0 if soil < 40 else 0.0,
                "pump_on": False,
                "irrigation_count": 3,
            },
            None,
        )


class Handler(BaseHTTPRequestHandler):
    runtime: Runtime

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _reply(self, status, headers, payload):
        raw = b"" if payload is None else json.dumps(payload).encode("utf-8")
        self.send_response(status)
        for key, value in headers.items():
            self.send_header(key, value)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        if raw:
            self.wfile.write(raw)

    def _dispatch(self, method):
        parsed = urlparse(self.path)
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length).decode("utf-8") if length else None
        query = parse_qs(parsed.query)
        status, headers, payload = handle(
            self.runtime, method, parsed.path, query, body
        )
        self._reply(status, headers, payload)

    def do_OPTIONS(self):
        self._dispatch("OPTIONS")

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")


def serve(host: str, port: int, thing: str, live: bool):
    runtime = Runtime()
    seed(runtime, thing=thing)
    stop = threading.Event()
    if live:
        threading.Thread(
            target=live_loop,
            args=(runtime, thing, 5.0, stop),
            daemon=True,
        ).start()

    Handler.runtime = runtime
    httpd = ThreadingHTTPServer((host, port), Handler)
    print(
        f"""
PotAI local  http://{host}:{port}
  App (este PC)     http://127.0.0.1:{port}
  Emulador Android  http://10.0.2.2:{port}
  Telefono          http://<IP-de-esta-PC>:{port}
  Thing Name        {thing}

GET  /plants
GET  /devices/{thing}/dashboard
POST /ingest     (simula un mensaje del ESP32)
Ctrl+C para parar.
"""
    )
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nparado")
    finally:
        stop.set()
        httpd.server_close()


def main():
    parser = argparse.ArgumentParser(description="API local de PotAI (sin AWS)")
    parser.add_argument("--host", default=os.environ.get("HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8080")))
    parser.add_argument("--thing", default=os.environ.get("THING_NAME", "psi3542-g01"))
    parser.add_argument("--no-live", action="store_true", help="no generar lecturas nuevas")
    args = parser.parse_args()
    serve(args.host, args.port, args.thing, live=not args.no_live)


if __name__ == "__main__":
    main()
