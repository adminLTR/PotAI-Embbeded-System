import os
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation

import boto3
from botocore.exceptions import ClientError


TABLE_NAME = os.environ.get("TABLE_NAME", "PSI3542Telemetry")

dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)


def _required(event, name):
    if name not in event:
        raise ValueError(f"missing field: {name}")
    return event[name]


def _integer_field(event, name, minimum=0):
    value = _required(event, name)
    if type(value) is not int or value < minimum:
        raise ValueError(f"invalid integer field: {name}")
    return value


def _decimal_field(event, name):
    value = _required(event, name)
    if isinstance(value, bool) or not isinstance(value, (int, float, str, Decimal)):
        raise ValueError(f"invalid numeric field: {name}")
    try:
        result = Decimal(str(value))
    except (InvalidOperation, ValueError):
        raise ValueError(f"invalid numeric field: {name}") from None
    if not result.is_finite():
        raise ValueError(f"non-finite numeric field: {name}")
    return result


def lambda_handler(event, context):
    try:
        thing_name = _required(event, "thingName")
        device_id = _required(event, "device_id")
        if not isinstance(thing_name, str) or not thing_name.startswith("psi3542-"):
            raise ValueError("invalid thingName")
        if not isinstance(device_id, str) or device_id != thing_name:
            raise ValueError("device_id differs from thingName")
        seq = _integer_field(event, "seq")
        valid = _required(event, "valid")
        if type(valid) is not bool:
            raise ValueError("valid must be JSON boolean")
        sample_uptime_ms = _integer_field(event, "sample_uptime_ms")
        ingested_at_ms = _integer_field(event, "ingested_at_ms", minimum=1)
        message_trace_id = _required(event, "message_trace_id")
        if not isinstance(message_trace_id, str) or not message_trace_id.strip():
            raise ValueError("invalid message_trace_id")
        temperature_c = _decimal_field(event, "temperature_c")
        humidity_pct = _decimal_field(event, "humidity_pct")
        if not (Decimal("0") <= humidity_pct <= Decimal("100")):
            raise ValueError("humidity_pct outside 0..100")
        ingested_at = (
            datetime.fromtimestamp(ingested_at_ms / 1000.0, tz=timezone.utc)
            .isoformat(timespec="milliseconds")
            .replace("+00:00", "Z")
        )
    except (ValueError, TypeError, OverflowError, OSError) as exc:
        print(f"REJECTED malformed telemetry: {exc}")
        return {"status": "rejected", "reason": str(exc)}

    ingestion_key = (
        f"{ingested_at_ms:013d}"
        f"#{seq:010d}"
        f"#{message_trace_id}"
    )
    item = {
        "thingName": thing_name,
        "ingestionKey": ingestion_key,
        "message_trace_id": message_trace_id,
        "ingested_at_ms": ingested_at_ms,
        "ingested_at": ingested_at,
        "device_id": device_id,
        "seq": seq,
        "temperature_c": temperature_c,
        "humidity_pct": humidity_pct,
        "valid": valid,
        "sample_uptime_ms": sample_uptime_ms,
    }

    try:
        table.put_item(
            Item=item,
            ConditionExpression="attribute_not_exists(ingestionKey)"
        )
    except ClientError as exc:
        code = exc.response["Error"]["Code"]
        if code == "ConditionalCheckFailedException":
            print(
                "DUPLICATE ignored "
                f"thingName={thing_name} seq={seq} trace={message_trace_id}"
            )
            return {"status": "duplicate", "thingName": thing_name, "seq": seq}
        raise

    print(
        "STORED "
        f"thingName={thing_name} seq={seq} trace={message_trace_id} "
        f"ingestionKey={ingestion_key}"
    )
    return {"status": "stored", "thingName": thing_name, "seq": seq}
