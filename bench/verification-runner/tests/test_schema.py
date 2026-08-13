"""T11-T12: schema validation - vacuous oracles rejected, flash-verify
constraints enforced, schema error blocks the run (exit 4)."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import runner as runner_mod
from runner import SchemaError, validate_scenario
from .helpers import flash_verify_scenario, scenario


class TestSchema(unittest.TestCase):
    def test_valid_scenarios(self):
        validate_scenario(scenario())
        validate_scenario(flash_verify_scenario())

    def test_vacuous_behavior_rejected(self):  # T11
        sc = scenario(oracle={"minFrames": 0, "maxCrcBadRatio": 1.0,
                              "requirePatterns": [], "forbidPatterns": []})
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_flash_verify_no_behavior_claim(self):  # T12
        with self.assertRaises(SchemaError):
            validate_scenario(flash_verify_scenario(
                oracle={"minFrames": 1, "maxCrcBadRatio": 1.0,
                        "requirePatterns": [], "forbidPatterns": []}))
        with self.assertRaises(SchemaError):
            validate_scenario(flash_verify_scenario(
                oracle={"minFrames": 0, "maxCrcBadRatio": 1.0,
                        "requirePatterns": ["x"], "forbidPatterns": []}))
        with self.assertRaises(SchemaError):
            validate_scenario(flash_verify_scenario(
                oracle={"minFrames": 0, "maxCrcBadRatio": 1.0,
                        "requirePatterns": [], "forbidPatterns": ["x"]}))

    def test_identity_required(self):
        sc = scenario()
        del sc["identity"]
        with self.assertRaises(SchemaError):
            validate_scenario(sc)
        sc = scenario()
        sc["identity"]["board"]["uid"] = "not-hex"
        with self.assertRaises(SchemaError):
            validate_scenario(sc)

    def test_field_types(self):
        with self.assertRaises(SchemaError):
            validate_scenario(scenario(
                capture={"port": "auto", "baud": 0, "parity": "E",
                         "durationS": 1, "maxBytes": 100}))
        with self.assertRaises(SchemaError):
            validate_scenario(scenario(
                oracle={"minFrames": 1, "maxCrcBadRatio": 1.5,
                        "requirePatterns": [], "forbidPatterns": []}))
        with self.assertRaises(SchemaError):
            validate_scenario(scenario(
                oracle={"minFrames": 1, "maxCrcBadRatio": 1.0,
                        "requirePatterns": ["([unclosed"],
                        "forbidPatterns": []}))

    def test_schema_error_exit_4_run_not_started(self):  # T11 exit path
        sc = scenario(oracle={"minFrames": 0, "maxCrcBadRatio": 1.0,
                              "requirePatterns": [], "forbidPatterns": []})
        with tempfile.TemporaryDirectory() as tmp:
            bad_path = Path(tmp) / "bad.json"
            bad_path.write_text(json.dumps(sc), encoding="utf-8")
            with patch("runner.run_loop") as m_run:
                code = runner_mod.main(["run", str(bad_path)])
        m_run.assert_not_called()
        self.assertEqual(code, 4)

    def test_shipped_scenarios_validate(self):
        # Drift guard: every shipped scenario must pass the executable
        # validator (design doc section 0.1).
        runner_dir = Path(__file__).resolve().parents[1]
        for name in ("uart-probe", "flash-boot-smoke"):
            path = runner_dir / "scenarios" / f"{name}.json"
            validate_scenario(json.loads(path.read_text(encoding="utf-8")))

    def test_validator_enforces_schema_required_fields(self):
        # Doc-code drift guard (design doc section 0.1, stdlib-only): every
        # field the normative scenario-v1.json marks required must be
        # rejected by the executable validator when missing. The validator
        # may be stricter, never looser.
        schema = json.loads(
            (Path(__file__).resolve().parents[1]
             / "schemas" / "scenario-v1.json").read_text(encoding="utf-8"))
        cases = []
        for label, path, required in [
            ("top", (), schema["required"]),
            ("identity", ("identity",),
             schema["properties"]["identity"]["required"]),
            ("board", ("identity", "board"),
             schema["properties"]["identity"]["properties"]["board"]["required"]),
            ("flash", ("flash",), schema["properties"]["flash"]["required"]),
            ("capture", ("capture",), schema["properties"]["capture"]["required"]),
            ("oracle", ("oracle",), schema["properties"]["oracle"]["required"]),
        ]:
            for field in required:
                cases.append((f"{label}.{field}", path, field))
        for label, path, field in cases:
            sc = scenario()
            node = sc
            for part in path:
                node = node[part]
            del node[field]
            with self.assertRaises(SchemaError,
                                   msg=f"schema-required {label} accepted"):
                validate_scenario(sc)

    def test_unreadable_scenario_file_exit_4(self):
        # Missing/unreadable scenario path: clean refusal, exit 4, run_loop
        # not started (same defect class as T7d for the checklist file).
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / "no-such-scenario.json"
            with patch("runner.run_loop") as m_run:
                code = runner_mod.main(["run", str(missing)])
        m_run.assert_not_called()
        self.assertEqual(code, 4)

    def test_id_must_be_slug(self):
        with self.assertRaises(SchemaError):
            validate_scenario(scenario(id="bad/id"))
        with self.assertRaises(SchemaError):
            validate_scenario(scenario(id="..\\escape"))
        validate_scenario(scenario(id="t-behavior"))


if __name__ == "__main__":
    unittest.main()
