"""Ingestion de PotAI (AWS Lambda disparada por IoT Core).

Dos tipos de mensaje, ambos validados antes de guardarse en DynamoDB:

  telemetry   psi3542/<thing>/telemetry    -> tabla PSI3542Telemetry
  irrigation  psi3542/<thing>/irrigation   -> tabla PSI3542Irrigation

La regla de IoT agrega thingName, ingested_at_ms y message_trace_id. La regla
de riego ademas agrega record_type='irrigation'; sin record_type el mensaje es
telemetria (contrato original A11, los campos PotAI son opcionales).
"""

import os
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation

import boto3
from botocore.exceptions import ClientError


TABLE_NAME = os.environ.get("TABLE_NAME", "PSI3542Telemetry")
IRRIGATION_TABLE_NAME = os.environ.get("IRRIGATION_TABLE_NAME", "PSI3542Irrigation")

TELEMETRY_TTL_S = 7 * 24 * 3600      # lecturas: 7 dias
IRRIGATION_TTL_S = 90 * 24 * 3600    # riegos: 90 dias

_cache = {}


def _table(name):
    if name not in _cache:
        _cache[name] = boto3.resource("dynamodb").Table(name)
    return _cache[name]


def _required(event, name):
    if name not in event:
        raise ValueError(f"missing field: {name}")
    return event[name]


def _integer_field(event, name, minimum=0):
    value = _required(event, name)
    if type(value) is not int or value < minimum:
        raise ValueError(f"invalid integer field: {name}")
    return value


def _decimal_field(event, name, minimum=None, maximum=None):
    value = _required(event, name)
    if isinstance(value, bool) or not isinstance(value, (int, float, str, Decimal)):
        raise ValueError(f"invalid numeric field: {name}")
    try:
        result = Decimal(str(value))
    except (InvalidOperation, ValueError):
        raise ValueError(f"invalid numeric field: {name}") from None
    if not result.is_finite():
        raise ValueError(f"non-finite numeric field: {name}")
    if minimum is not None and result < Decimal(minimum):
        raise ValueError(f"{name} below {minimum}")
    if maximum is not None and result > Decimal(maximum):
        raise ValueError(f"{name} above {maximum}")
    return result


def _bool_field(event, name):
    value = _required(event, name)
    if type(value) is not bool:
        raise ValueError(f"{name} must be JSON boolean")
    return value


def _common_fields(event):
    thing_name = _required(event, "thingName")
    device_id = _required(event, "device_id")
    if not isinstance(thing_name, str) or not thing_name.startswith("psi3542-"):
        raise ValueError("invalid thingName")
    if not isinstance(device_id, str) or device_id != thing_name:
        raise ValueError("device_id differs from thingName")
    seq = _integer_field(event, "seq")
    ingested_at_ms = _integer_field(event, "ingested_at_ms", minimum=1)
    message_trace_id = _required(event, "message_trace_id")
    if not isinstance(message_trace_id, str) or not message_trace_id.strip():
        raise ValueError("invalid message_trace_id")
    ingested_at = (
        datetime.fromtimestamp(ingested_at_ms / 1000.0, tz=timezone.utc)
        .isoformat(timespec="milliseconds")
        .replace("+00:00", "Z")
    )
    return {
        "thingName": thing_name,
        "ingestionKey": f"{ingested_at_ms:013d}#{seq:010d}#{message_trace_id}",
        "message_trace_id": message_trace_id,
        "ingested_at_ms": ingested_at_ms,
        "ingested_at": ingested_at,
        "device_id": device_id,
        "seq": seq,
    }


def build_telemetry_item(event):
    item = _common_fields(event)
    item["temperature_c"] = _decimal_field(event, "temperature_c")
    item["humidity_pct"] = _decimal_field(event, "humidity_pct", 0, 100)
    item["valid"] = _bool_field(event, "valid")
    item["sample_uptime_ms"] = _integer_field(event, "sample_uptime_ms")

    # --- Campos PotAI (opcionales: el firmware A10 antiguo no los envia) ---
    soil_valid = _bool_field(event, "soil_valid") if "soil_valid" in event else False
    item["soil_valid"] = soil_valid
    if soil_valid:
        item["soil_moisture_pct"] = _decimal_field(event, "soil_moisture_pct", 0, 100)
    if "plant_id" in event:
        item["plant_id"] = _integer_field(event, "plant_id", minimum=1)
    if "ml_predicted" in event:
        item["ml_predicted"] = _decimal_field(event, "ml_predicted", 0)
    if "pump_on" in event:
        item["pump_on"] = _bool_field(event, "pump_on")
    if "irrigation_count" in event:
        item["irrigation_count"] = _integer_field(event, "irrigation_count")

    item["expires_at"] = item["ingested_at_ms"] // 1000 + TELEMETRY_TTL_S
    return item


def build_irrigation_item(event):
    item = _common_fields(event)
    item["plant_id"] = _integer_field(event, "plant_id", minimum=1)
    item["soil_moisture_pct"] = _decimal_field(event, "soil_moisture_pct", 0, 100)
    item["temperature_c"] = _decimal_field(event, "temperature_c")
    item["ml_predicted"] = _decimal_field(event, "ml_predicted", 0)
    item["ml_delivered"] = _decimal_field(event, "ml_delivered", 0)
    item["duration_ms"] = _integer_field(event, "duration_ms")
    item["pump_uptime_ms"] = _integer_field(event, "pump_uptime_ms")
    item["expires_at"] = item["ingested_at_ms"] // 1000 + IRRIGATION_TTL_S
    return item


def lambda_handler(event, context):
    record_type = event.get("record_type", "telemetry")
    try:
        if record_type == "telemetry":
            item = build_telemetry_item(event)
            table_name = TABLE_NAME
        elif record_type == "irrigation":
            item = build_irrigation_item(event)
            table_name = IRRIGATION_TABLE_NAME
        else:
            raise ValueError(f"unknown record_type: {record_type}")
    except (ValueError, TypeError, OverflowError, OSError) as exc:
        print(f"REJECTED malformed {record_type}: {exc}")
        return {"status": "rejected", "reason": str(exc)}

    try:
        _table(table_name).put_item(
            Item=item,
            ConditionExpression="attribute_not_exists(ingestionKey)",
        )
    except ClientError as exc:
        code = exc.response["Error"]["Code"]
        if code == "ConditionalCheckFailedException":
            print(
                f"DUPLICATE ignored type={record_type} "
                f"thingName={item['thingName']} seq={item['seq']} trace={item['message_trace_id']}"
            )
            return {"status": "duplicate", "thingName": item["thingName"], "seq": item["seq"]}
        raise

    print(
        f"STORED type={record_type} "
        f"thingName={item['thingName']} seq={item['seq']} trace={item['message_trace_id']} "
        f"ingestionKey={item['ingestionKey']}"
    )
    return {"status": "stored", "thingName": item["thingName"], "seq": item["seq"]}
