"""T18-T22: scenario v2 validation, readback oracle (PASS/FAIL/TIMEOUT),
evidence bundle for readback artifacts (ticket #63 runner extension)."""

import json
import os
import tempfile
import unittest
from pathlib import Path

from runner import (SchemaError, check_evidence, evaluate_readback_oracle,
                    validate_scenario)

BENCH = {"part": "STM32F405RG", "uid": "002900363033470336363131"}


def v2_scenario(**overrides):
    sc = {
        "schemaVersion": 2,
        "id": "sensing-acquire",
        "title": "sensing acquisition",
        "type": "behavior",
        "phase": "L4",
        "identity": {"board": BENCH},
        "flash": {"required": True, "env": "firmware"},
        "readback": {
            "addr": 0x20011000,
            "words": 52,
            "windowS": 10.0,
            "rules": [
                {"name": "magic", "offset": 0, "kind": "eq", "expect": 0x53454E53},
                {"name": "tof_cadence", "offset": 16, "kind": "delta",
                 "minDelta": 180},
                {"name": "tof_fwd_state", "offset": 23, "kind": "eq",
                 "expect": 3},
            ],
        },
    }
    sc.update(overrides)
    return sc


def snap(words):
    return {"snapA": [0] * 52, "snapB": list(words), "windowS": 10.0}


class TestValidateV2(unittest.TestCase):
    def test_valid_v2(self):  # T18
        sc = v2_scenario()
        self.assertEqual(validate_scenario(sc), sc)

    def test_readback_required(self):  # T19a
        sc = v2_scenario()
        del sc["readback"]
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_rule_kind_missing(self):  # T19b
        sc = v2_scenario()
        sc["readback"]["rules"][1] = {"name": "x", "offset": 0}
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_delta_rule_needs_min_delta(self):  # T19c
        sc = v2_scenario()
        sc["readback"]["rules"][1] = {"name": "x", "offset": 0, "kind": "delta"}
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_eq_rule_needs_expect(self):  # T19d
        sc = v2_scenario()
        sc["readback"]["rules"][0] = {"name": "x", "offset": 0, "kind": "eq"}
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_max_rule_needs_max(self):  # T19e
        sc = v2_scenario()
        sc["readback"]["rules"][0] = {"name": "x", "offset": 0, "kind": "max"}
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_max_rule_valid(self):  # T19f
        sc = v2_scenario()
        sc["readback"]["rules"][0] = {"name": "x", "offset": 14, "kind": "max",
                                      "max": 300}
        self.assertEqual(validate_scenario(sc), sc)

    def test_bad_version(self):
        sc = v2_scenario(schemaVersion=3)
        with self.assertRaises(SchemaError):
            validate_scenario(sc)


class TestReadbackOracle(unittest.TestCase):
    def test_pass(self):  # T20
        words = [0] * 52
        words[0] = 0x53454E53
        words[16] = 200   # tof cadence: 200 >= 180
        words[23] = 3     # 0x0A Faulted
        verdict, reasons = evaluate_readback_oracle(v2_scenario(), snap(words))
        self.assertEqual(verdict, "PASS")
        self.assertEqual(reasons, [])

    def test_fail_eq_mismatch(self):  # T21a
        words = [0] * 52
        words[0] = 0x53454E53
        words[16] = 200
        words[23] = 1     # Healthy, expected Faulted
        verdict, reasons = evaluate_readback_oracle(v2_scenario(), snap(words))
        self.assertEqual(verdict, "FAIL")
        self.assertIn("tof_fwd_state", reasons[0])

    def test_fail_magic(self):  # T21b
        words = [0] * 52
        words[16] = 200
        verdict, reasons = evaluate_readback_oracle(v2_scenario(), snap(words))
        self.assertEqual(verdict, "FAIL")
        self.assertIn("magic", reasons[0])

    def test_timeout_low_cadence(self):  # T22
        words = [0] * 52
        words[0] = 0x53454E53
        words[16] = 50    # delta 50 < 180 over the window
        words[23] = 3
        verdict, reasons = evaluate_readback_oracle(v2_scenario(), snap(words))
        self.assertEqual(verdict, "TIMEOUT")
        self.assertIn("tof_cadence", reasons[0])

    def test_max_pass(self):  # T22b
        sc = v2_scenario()
        sc["readback"]["rules"].append(
            {"name": "age", "offset": 14, "kind": "max", "max": 300})
        words = [0] * 52
        words[0] = 0x53454E53
        words[16] = 200
        words[23] = 3
        words[14] = 12  # fresh
        verdict, reasons = evaluate_readback_oracle(sc, snap(words))
        self.assertEqual(verdict, "PASS")
        self.assertEqual(reasons, [])

    def test_max_fail(self):  # T22c
        sc = v2_scenario()
        sc["readback"]["rules"].append(
            {"name": "age", "offset": 14, "kind": "max", "max": 300})
        words = [0] * 52
        words[0] = 0x53454E53
        words[16] = 200
        words[23] = 3
        words[14] = 500  # stale
        verdict, reasons = evaluate_readback_oracle(sc, snap(words))
        self.assertEqual(verdict, "FAIL")
        self.assertIn("age", reasons[0])


class TestEvidenceV2(unittest.TestCase):
    def test_check_evidence_readback(self):
        result = {
            "schemaVersion": 2,
            "verdict": "PASS",
            "scenario": {"id": "sensing-acquire", "schemaVersion": 2},
            "evidence": {"complete": True, "missing": []},
            "normalized": {"snapA": [0], "snapB": [0], "windowS": 10.0},
        }
        with tempfile.TemporaryDirectory() as d:
            for name in ("raw-sensing-acquire-a.mdw", "raw-sensing-acquire-b.mdw"):
                with open(os.path.join(d, name), "w", encoding="utf-8") as f:
                    f.write("0x20011000: 00000000\n")
            missing = check_evidence(result, Path(d))
            self.assertEqual(missing, [])

    def test_check_evidence_readback_missing(self):
        result = {
            "schemaVersion": 2,
            "verdict": "PASS",
            "scenario": {"id": "sensing-acquire", "schemaVersion": 2},
            "evidence": {"complete": True, "missing": []},
            "normalized": {"snapA": [0], "snapB": [0], "windowS": 10.0},
        }
        with tempfile.TemporaryDirectory() as d:
            missing = check_evidence(result, Path(d))
            self.assertTrue(any("rawArtifact" in m for m in missing))


if __name__ == "__main__":
    unittest.main()
