"""Pruebas locales del backend PotAI. No necesitan AWS ni boto3:

    cd 02_cloud_aws
    python -m unittest discover -s tests -v
"""

import importlib.util
import json
import sys
import types
import unittest
from decimal import Decimal
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


# ------------------------------------------------------------ boto3 falso

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

    def put_item(self, Item, ConditionExpression=None):
        for it in self.items:
            if it["thingName"] == Item["thingName"] and it["ingestionKey"] == Item["ingestionKey"]:
                raise FakeClientError("ConditionalCheckFailedException")
        self.items.append(Item)

    def query(self, KeyConditionExpression, ScanIndexForward=True, Limit=None, ConsistentRead=False):
        rows = self.items
        for op, name, value in KeyConditionExpression.checks:
            rows = [r for r in rows if (r[name] == value if op == "eq" else r[name] >= value)]
        rows = sorted(rows, key=lambda r: r["ingestionKey"], reverse=not ScanIndexForward)
        return {"Items": rows[:Limit] if Limit else rows}


class FakeIot:
    def __init__(self):
        self.published = []

    def publish(self, **kwargs):
        self.published.append(kwargs)


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


def _fake_modules():
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
    return {
        "boto3": boto3,
        "boto3.dynamodb": dynamodb,
        "boto3.dynamodb.conditions": conditions,
        "botocore": botocore,
        "botocore.exceptions": exceptions,
    }


class BackendCase(unittest.TestCase):
    NOW = 1_800_000_000_000

    def setUp(self):
        patcher = mock.patch.dict(sys.modules, _fake_modules())
        patcher.start()
        self.addCleanup(patcher.stop)

        self.ingest = _load("potai_ingest", ROOT / "lambda_ingest" / "lambda_function.py")
        self.api = _load("potai_api", ROOT / "lambda_api" / "lambda_function.py", ROOT / "lambda_api")

        self.telemetry = FakeTable()
        self.irrigation = FakeTable()
        self.iot = FakeIot()
        self.ingest._cache.update({"PSI3542Telemetry": self.telemetry, "PSI3542Irrigation": self.irrigation})
        self.api._cache.update({"telemetry": self.telemetry, "irrigation": self.irrigation, "iot": self.iot})
        self.api.now_ms = lambda: self.NOW

    # --- utilidades
    def telemetry_event(self, **over):
        event = {
            "thingName": "psi3542-g01", "device_id": "psi3542-g01", "seq": 1,
            "temperature_c": 25.0, "humidity_pct": 60.0, "valid": True, "sample_uptime_ms": 5000,
            "ingested_at_ms": self.NOW - 2000, "message_trace_id": "trace-1",
            "soil_moisture_pct": 35.5, "soil_valid": True, "plant_id": 4,
            "ml_predicted": 120.0, "pump_on": False, "irrigation_count": 2,
        }
        event.update(over)
        return event

    def irrigation_event(self, **over):
        event = {
            "thingName": "psi3542-g01", "device_id": "psi3542-g01", "seq": 1, "record_type": "irrigation",
            "plant_id": 4, "soil_moisture_pct": 28.0, "temperature_c": 26.0,
            "ml_predicted": 120.0, "ml_delivered": 118.5, "duration_ms": 5900, "pump_uptime_ms": 99999,
            "ingested_at_ms": self.NOW - 3600_000, "message_trace_id": "trace-irr-1",
        }
        event.update(over)
        return event

    def call(self, route, thing="psi3542-g01", query=None, body=None):
        event = {"routeKey": route, "pathParameters": {"thingName": thing} if thing else {}}
        if query:
            event["queryStringParameters"] = query
        if body is not None:
            event["body"] = body if isinstance(body, str) else json.dumps(body)
        result = self.api.lambda_handler(event, None)
        return result["statusCode"], json.loads(result["body"])


class IngestTests(BackendCase):
    def test_telemetry_with_potai_fields_is_stored(self):
        out = self.ingest.lambda_handler(self.telemetry_event(), None)
        self.assertEqual(out["status"], "stored")
        item = self.telemetry.items[0]
        self.assertEqual(item["plant_id"], 4)
        self.assertEqual(item["soil_moisture_pct"], Decimal("35.5"))
        self.assertTrue(item["soil_valid"])
        self.assertIn("expires_at", item)

    def test_legacy_telemetry_without_potai_fields_still_works(self):
        event = self.telemetry_event()
        for name in ("soil_moisture_pct", "soil_valid", "plant_id", "ml_predicted", "pump_on", "irrigation_count"):
            del event[name]
        self.assertEqual(self.ingest.lambda_handler(event, None)["status"], "stored")
        self.assertFalse(self.telemetry.items[0]["soil_valid"])

    def test_invalid_soil_value_is_ignored_when_soil_not_valid(self):
        event = self.telemetry_event(soil_valid=False, soil_moisture_pct=0.0)
        self.assertEqual(self.ingest.lambda_handler(event, None)["status"], "stored")
        self.assertNotIn("soil_moisture_pct", self.telemetry.items[0])

    def test_soil_out_of_range_is_rejected(self):
        out = self.ingest.lambda_handler(self.telemetry_event(soil_moisture_pct=140), None)
        self.assertEqual(out["status"], "rejected")
        self.assertEqual(self.telemetry.items, [])

    def test_bad_types_are_rejected(self):
        for over in ({"valid": "true"}, {"pump_on": 1}, {"plant_id": 0}, {"humidity_pct": 120}, {"device_id": "otro"}):
            out = self.ingest.lambda_handler(self.telemetry_event(**over), None)
            self.assertEqual(out["status"], "rejected", over)

    def test_duplicate_is_ignored(self):
        self.ingest.lambda_handler(self.telemetry_event(), None)
        self.assertEqual(self.ingest.lambda_handler(self.telemetry_event(), None)["status"], "duplicate")
        self.assertEqual(len(self.telemetry.items), 1)

    def test_irrigation_goes_to_its_own_table(self):
        self.assertEqual(self.ingest.lambda_handler(self.irrigation_event(), None)["status"], "stored")
        self.assertEqual(len(self.irrigation.items), 1)
        self.assertEqual(self.telemetry.items, [])

    def test_irrigation_requires_fields_and_valid_values(self):
        event = self.irrigation_event()
        del event["ml_delivered"]
        self.assertEqual(self.ingest.lambda_handler(event, None)["status"], "rejected")
        self.assertEqual(self.ingest.lambda_handler(self.irrigation_event(ml_delivered=-1), None)["status"], "rejected")

    def test_unknown_record_type_is_rejected(self):
        self.assertEqual(self.ingest.lambda_handler(self.telemetry_event(record_type="xyz"), None)["status"], "rejected")


class ApiTests(BackendCase):
    def seed(self):
        self.ingest.lambda_handler(self.telemetry_event(seq=1, message_trace_id="a", ingested_at_ms=self.NOW - 8000), None)
        self.ingest.lambda_handler(self.telemetry_event(seq=2, message_trace_id="b", ingested_at_ms=self.NOW - 5000, soil_moisture_pct=33.0), None)
        self.ingest.lambda_handler(self.telemetry_event(seq=3, message_trace_id="c", soil_valid=False, soil_moisture_pct=0), None)
        self.ingest.lambda_handler(self.irrigation_event(seq=1, message_trace_id="i1", ingested_at_ms=self.NOW - 3600_000), None)
        self.ingest.lambda_handler(self.irrigation_event(seq=2, message_trace_id="i2", ingested_at_ms=self.NOW - 1800_000, ml_delivered=100), None)
        self.ingest.lambda_handler(self.irrigation_event(seq=3, message_trace_id="i3", ingested_at_ms=self.NOW - 3 * 24 * 3600_000), None)

    def test_plants_catalog_matches_project_species(self):
        status, body = self.call("GET /plants", thing=None)
        self.assertEqual(status, 200)
        names = [p["common_name"] for p in body["plants"]]
        self.assertEqual(
            names,
            ["ajo", "geranio", "hierbabuena", "menta", "oregano", "orquidea", "rosachina", "tomatecherry"],
        )
        self.assertEqual([p["id"] for p in body["plants"]], list(range(1, 9)))
        menta = body["plants"][3]
        self.assertEqual(menta["scientific_name"], "Mentha")
        self.assertEqual(menta["optimal_humidity_percent"], 70)

    def test_latest_keeps_original_contract_and_adds_potai(self):
        self.seed()
        status, body = self.call("GET /devices/{thingName}/telemetry/latest")
        self.assertEqual(status, 200)
        for key in ("thingName", "seq", "temperature_c", "humidity_pct", "valid", "sample_uptime_ms", "ingested_at_ms", "ingested_at"):
            self.assertIn(key, body)
        self.assertEqual(body["seq"], 3)
        self.assertIsNone(body["soil_moisture_pct"])  # soil_valid=False en la ultima
        self.assertEqual(body["plant_id"], 4)
        self.assertEqual(body["age_ms"], 2000)

    def test_latest_404_when_empty(self):
        status, body = self.call("GET /devices/{thingName}/telemetry/latest")
        self.assertEqual((status, body["error"]), (404, "telemetry_not_found"))

    def test_dashboard(self):
        self.seed()
        status, body = self.call("GET /devices/{thingName}/dashboard")
        self.assertEqual(status, 200)
        self.assertTrue(body["online"])
        self.assertEqual(body["plant"]["common_name"], "menta")
        self.assertEqual(body["irrigation"]["count_24h"], 2)
        self.assertEqual(body["irrigation"]["ml_24h"], 218.5)
        self.assertEqual(body["irrigation"]["last"]["seq"], 2)

    def test_dashboard_offline_and_empty(self):
        status, body = self.call("GET /devices/{thingName}/dashboard")
        self.assertEqual(status, 200)
        self.assertFalse(body["online"])
        self.assertIsNone(body["latest"])
        self.assertIsNone(body["plant"])
        self.ingest.lambda_handler(self.telemetry_event(ingested_at_ms=self.NOW - 120_000), None)
        status, body = self.call("GET /devices/{thingName}/dashboard")
        self.assertFalse(body["online"])

    def test_history_limit_and_order(self):
        self.seed()
        status, body = self.call("GET /devices/{thingName}/telemetry/history", query={"limit": "2"})
        self.assertEqual(status, 200)
        self.assertEqual(len(body["items"]), 2)
        self.assertGreater(body["items"][0]["ingested_at_ms"], body["items"][1]["ingested_at_ms"])
        status, body = self.call("GET /devices/{thingName}/telemetry/history", query={"limit": "9999"})
        self.assertEqual(len(body["items"]), 3)

    def test_irrigations_newest_first(self):
        self.seed()
        status, body = self.call("GET /devices/{thingName}/irrigations")
        self.assertEqual(status, 200)
        self.assertEqual([i["seq"] for i in body["items"]], [2, 1, 3])
        times = [i["ingested_at_ms"] for i in body["items"]]
        self.assertEqual(times, sorted(times, reverse=True))

    def test_command_contract_unchanged(self):
        status, body = self.call("POST /devices/{thingName}/command", body={"led": True})
        self.assertEqual((status, body["accepted"]), (202, True))
        self.assertEqual(self.iot.published[0]["topic"], "psi3542/psi3542-g01/command")
        self.assertEqual(self.iot.published[0]["payload"], b'{"led":true}')
        self.assertEqual(self.call("POST /devices/{thingName}/command", body={"led": "x"})[0], 400)
        self.assertEqual(self.call("POST /devices/{thingName}/command", body="{no json")[0], 400)

    def test_invalid_thing_and_unknown_route(self):
        self.assertEqual(self.call("GET /devices/{thingName}/dashboard", thing="otro")[0], 400)
        self.assertEqual(self.call("GET /devices/{thingName}/nada")[0], 404)

    def test_internal_errors_do_not_leak(self):
        self.api._cache["telemetry"] = None
        status, body = self.call("GET /devices/{thingName}/dashboard")
        self.assertEqual((status, body), (500, {"error": "internal_error"}))


if __name__ == "__main__":
    unittest.main()
