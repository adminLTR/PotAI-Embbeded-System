"""Pruebas del servidor local (sin abrir un puerto ni AWS)."""

import json
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import local_server  # noqa: E402


class LocalServerTests(unittest.TestCase):
    def setUp(self):
        self.rt = local_server.Runtime()
        local_server.seed(self.rt)

    def call(self, method, path, query=None, body=None):
        status, _headers, payload = local_server.handle(
            self.rt, method, path, query or {}, body
        )
        return status, payload

    def test_health_and_plants(self):
        self.assertEqual(self.call("GET", "/health")[0], 200)
        status, body = self.call("GET", "/plants")
        self.assertEqual(status, 200)
        self.assertEqual(len(body["plants"]), 8)

    def test_dashboard_uses_seeded_menta(self):
        status, body = self.call("GET", "/devices/psi3542-g01/dashboard")
        self.assertEqual(status, 200)
        self.assertTrue(body["online"])
        self.assertEqual(body["plant"]["common_name"], "menta")
        self.assertGreaterEqual(body["irrigation"]["count_24h"], 1)

    def test_ingest_then_history(self):
        payload = json.dumps(
            {
                "device_id": "psi3542-g01",
                "temperature_c": 22.0,
                "humidity_pct": 40.0,
                "valid": True,
                "sample_uptime_ms": 1,
                "soil_valid": True,
                "soil_moisture_pct": 19.0,
                "plant_id": 1,
                "ml_predicted": 80.0,
                "pump_on": False,
                "irrigation_count": 0,
            }
        )
        status, body = self.call("POST", "/ingest", body=payload)
        self.assertEqual((status, body["status"]), (200, "stored"))
        status, body = self.call("GET", "/devices/psi3542-g01/telemetry/latest")
        self.assertEqual(status, 200)
        self.assertEqual(body["plant_id"], 1)
        self.assertEqual(body["soil_moisture_pct"], 19.0)

    def test_unknown_route(self):
        self.assertEqual(self.call("GET", "/nope")[0], 404)


if __name__ == "__main__":
    unittest.main()
