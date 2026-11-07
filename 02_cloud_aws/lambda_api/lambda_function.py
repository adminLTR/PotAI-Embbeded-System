import base64
import json
import os
from decimal import Decimal

import boto3
from boto3.dynamodb.conditions import Key


TABLE_NAME = os.environ.get("TABLE_NAME", "PSI3542Telemetry")
IOT_DATA_ENDPOINT = os.environ["IOT_DATA_ENDPOINT"]


dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)

iot_data = boto3.client(
    "iot-data",
    endpoint_url=IOT_DATA_ENDPOINT,
)


def response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {"content-type": "application/json"},
        "body": json.dumps(body),
    }


def plain_number(value):
    if isinstance(value, Decimal):
        if value == value.to_integral_value():
            return int(value)
        return float(value)
    return value


def validate_thing_name(value):
    if not isinstance(value, str) or not value.startswith("psi3542-"):
        return False
    allowed = set(
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789-_"
    )
    return bool(value) and all(c in allowed for c in value)


def get_latest(thing_name):
    result = table.query(
        KeyConditionExpression=Key("thingName").eq(thing_name),
        ScanIndexForward=False,
        Limit=1,
        ConsistentRead=True,
    )
    items = result.get("Items", [])
    if not items:
        return response(
            404,
            {"error": "telemetry_not_found", "thingName": thing_name},
        )

    item = items[0]
    return response(
        200,
        {
            "thingName": item["thingName"],
            "seq": plain_number(item["seq"]),
            "temperature_c": plain_number(item["temperature_c"]),
            "humidity_pct": plain_number(item["humidity_pct"]),
            "valid": item["valid"],
            "sample_uptime_ms": plain_number(item["sample_uptime_ms"]),
            "ingested_at_ms": plain_number(item["ingested_at_ms"]),
            "ingested_at": item["ingested_at"],
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

    iot_data.publish(
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
    path_parameters = event.get("pathParameters") or {}
    thing_name = path_parameters.get("thingName")

    if not validate_thing_name(thing_name):
        return response(400, {"error": "invalid_thing_name"})

    if route_key == "GET /devices/{thingName}/telemetry/latest":
        return get_latest(thing_name)

    if route_key == "POST /devices/{thingName}/command":
        return post_command(thing_name, event)

    return response(404, {"error": "route_not_found"})
