"""API HTTP de PotAI (AWS Lambda detras de API Gateway).

Es el "puente" entre lo que el ESP32 guardo en DynamoDB y la aplicacion:

  GET  /plants                                    catalogo de plantas
  GET  /devices/{thingName}/dashboard             todo lo que necesita la pantalla de inicio
  GET  /devices/{thingName}/telemetry/latest      ultima lectura (contrato original A11)
  GET  /devices/{thingName}/telemetry/history     lecturas recientes (?limit=1..200)
  GET  /devices/{thingName}/irrigations           riegos recientes (?limit=1..100)
  POST /devices/{thingName}/command               {"led": true|false} (contrato original A11)
"""

import base64
import json
import os
import time
from decimal import Decimal

import boto3
from boto3.dynamodb.conditions import Key

from plants import plant_catalog, plant_info


TABLE_NAME = os.environ.get("TABLE_NAME", "PSI3542Telemetry")
IRRIGATION_TABLE_NAME = os.environ.get("IRRIGATION_TABLE_NAME", "PSI3542Irrigation")
IOT_DATA_ENDPOINT = os.environ.get("IOT_DATA_ENDPOINT", "")

# El ESP32 envia telemetria cada ~3 s: sin datos por 30 s => "sin conexion".
ONLINE_WINDOW_MS = 30_000
DAY_MS = 24 * 60 * 60 * 1000

_cache = {}


def _resources():
    """Crea los clientes AWS la primera vez que se necesitan."""
    if not _cache:
        ddb = boto3.resource("dynamodb")
        _cache["telemetry"] = ddb.Table(TABLE_NAME)
        _cache["irrigation"] = ddb.Table(IRRIGATION_TABLE_NAME)
        _cache["iot"] = boto3.client("iot-data", endpoint_url=IOT_DATA_ENDPOINT)
    return _cache


def _json_default(value):
    if isinstance(value, Decimal):
        return int(value) if value == value.to_integral_value() else float(value)
    raise TypeError(f"not serializable: {type(value)}")


def response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {"content-type": "application/json"},
        "body": json.dumps(body, default=_json_default),
    }


def plain_number(value):
    if isinstance(value, Decimal):
        if value == value.to_integral_value():
            return int(value)
        return float(value)
    return value


def now_ms():
    return int(time.time() * 1000)


def validate_thing_name(value):
    if not isinstance(value, str) or not value.startswith("psi3542-"):
        return False
    allowed = set(
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789-_"
    )
    return bool(value) and all(c in allowed for c in value)


def parse_limit(event, default, maximum):
    params = event.get("queryStringParameters") or {}
    raw = params.get("limit")
    if raw is None:
        return default
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return default
    return max(1, min(value, maximum))


# ---------------------------------------------------------------- vistas

def _optional(item, name):
    return plain_number(item[name]) if name in item else None


def latest_view(item, current_ms):
    """Lectura completa. Los campos originales A11 se mantienen intactos."""
    soil_valid = bool(item.get("soil_valid", False))
    ingested_at_ms = plain_number(item["ingested_at_ms"])
    return {
        "thingName": item["thingName"],
        "seq": plain_number(item["seq"]),
        "temperature_c": plain_number(item["temperature_c"]),
        "humidity_pct": plain_number(item["humidity_pct"]),
        "valid": item["valid"],
        "sample_uptime_ms": plain_number(item["sample_uptime_ms"]),
        "ingested_at_ms": ingested_at_ms,
        "ingested_at": item["ingested_at"],
        # PotAI
        "age_ms": max(0, current_ms - ingested_at_ms),
        "soil_valid": soil_valid,
        "soil_moisture_pct": _optional(item, "soil_moisture_pct") if soil_valid else None,
        "plant_id": _optional(item, "plant_id"),
        "ml_predicted": _optional(item, "ml_predicted"),
        "pump_on": bool(item.get("pump_on", False)),
        "irrigation_count": _optional(item, "irrigation_count"),
    }


def history_point(item):
    soil_valid = bool(item.get("soil_valid", False))
    return {
        "ingested_at_ms": plain_number(item["ingested_at_ms"]),
        "temperature_c": plain_number(item["temperature_c"]),
        "humidity_pct": plain_number(item["humidity_pct"]),
        "soil_moisture_pct": _optional(item, "soil_moisture_pct") if soil_valid else None,
        "valid": item["valid"],
        "pump_on": bool(item.get("pump_on", False)),
    }


def irrigation_view(item):
    return {
        "seq": plain_number(item["seq"]),
        "plant_id": plain_number(item["plant_id"]),
        "soil_moisture_pct": plain_number(item["soil_moisture_pct"]),
        "temperature_c": plain_number(item["temperature_c"]),
        "ml_predicted": plain_number(item["ml_predicted"]),
        "ml_delivered": plain_number(item["ml_delivered"]),
        "duration_ms": plain_number(item["duration_ms"]),
        "ingested_at_ms": plain_number(item["ingested_at_ms"]),
        "ingested_at": item["ingested_at"],
    }


def _query_latest(thing_name, limit):
    result = _resources()["telemetry"].query(
        KeyConditionExpression=Key("thingName").eq(thing_name),
        ScanIndexForward=False,
        Limit=limit,
        ConsistentRead=True,
    )
    return result.get("Items", [])


def _query_irrigations(thing_name, limit):
    result = _resources()["irrigation"].query(
        KeyConditionExpression=Key("thingName").eq(thing_name),
        ScanIndexForward=False,
        Limit=limit,
    )
    return result.get("Items", [])


# ---------------------------------------------------------------- rutas

def get_plants():
    return response(200, {"plants": plant_catalog()})


def get_latest(thing_name):
    items = _query_latest(thing_name, 1)
    if not items:
        return response(
            404,
            {"error": "telemetry_not_found", "thingName": thing_name},
        )
    return response(200, latest_view(items[0], now_ms()))


def get_history(thing_name, event):
    limit = parse_limit(event, default=60, maximum=200)
    items = _query_latest(thing_name, limit)
    return response(
        200,
        {"thingName": thing_name, "items": [history_point(i) for i in items]},
    )


def get_irrigations(thing_name, event):
    limit = parse_limit(event, default=30, maximum=100)
    items = _query_irrigations(thing_name, limit)
    return response(
        200,
        {"thingName": thing_name, "items": [irrigation_view(i) for i in items]},
    )


def get_dashboard(thing_name):
    current_ms = now_ms()
    latest_items = _query_latest(thing_name, 1)
    latest = latest_view(latest_items[0], current_ms) if latest_items else None

    recent = [irrigation_view(i) for i in _query_irrigations(thing_name, 100)]
    last_24h = [r for r in recent if r["ingested_at_ms"] >= current_ms - DAY_MS]

    plant = plant_info(latest["plant_id"]) if latest else None
    return response(
        200,
        {
            "thingName": thing_name,
            "server_time_ms": current_ms,
            "online": bool(latest) and latest["age_ms"] <= ONLINE_WINDOW_MS,
            "latest": latest,
            "plant": plant,
            "irrigation": {
                "last": recent[0] if recent else None,
                "count_24h": len(last_24h),
                "ml_24h": round(sum(r["ml_delivered"] for r in last_24h), 1),
            },
        },
    )


def parse_json_body(event):
    raw = event.get("body")
    if raw is None:
        raise ValueError("missing body")
    if event.get("isBase64Encoded", False):
        raw = base64.b64decode(raw).decode("utf-8")
    return json.loads(raw)


def post_command(thing_name, event):
    try:
        body = parse_json_body(event)
    except (ValueError, json.JSONDecodeError, UnicodeDecodeError):
        return response(400, {"error": "invalid_json"})

    if (
        not isinstance(body, dict)
        or set(body.keys()) != {"led"}
        or type(body["led"]) is not bool
    ):
        return response(
            400,
            {"error": "invalid_command", "expected": {"led": "boolean"}},
        )

    payload = json.dumps(
        {"led": body["led"]},
        separators=(",", ":"),
    ).encode("utf-8")
    topic = f"psi3542/{thing_name}/command"

    _resources()["iot"].publish(
        topic=topic,
        qos=1,
        retain=False,
        payload=payload,
    )

    print(
        "COMMAND_ACCEPTED "
        f"thingName={thing_name} "
        f"led={body['led']} "
        f"topic={topic}"
    )

    return response(
        202,
        {
            "accepted": True,
            "thingName": thing_name,
            "command": {"led": body["led"]},
        },
    )


def lambda_handler(event, context):
    route_key = event.get("routeKey", "")

    if route_key == "GET /plants":
        return get_plants()

    path_parameters = event.get("pathParameters") or {}
    thing_name = path_parameters.get("thingName")

    if not validate_thing_name(thing_name):
        return response(400, {"error": "invalid_thing_name"})

    try:
        if route_key == "GET /devices/{thingName}/dashboard":
            return get_dashboard(thing_name)

        if route_key == "GET /devices/{thingName}/telemetry/latest":
            return get_latest(thing_name)

        if route_key == "GET /devices/{thingName}/telemetry/history":
            return get_history(thing_name, event)

        if route_key == "GET /devices/{thingName}/irrigations":
            return get_irrigations(thing_name, event)

        if route_key == "POST /devices/{thingName}/command":
            return post_command(thing_name, event)
    except Exception as exc:  # noqa: BLE001 - la API nunca devuelve trazas
        print(f"INTERNAL_ERROR route={route_key} error={exc!r}")
        return response(500, {"error": "internal_error"})

    return response(404, {"error": "route_not_found"})
