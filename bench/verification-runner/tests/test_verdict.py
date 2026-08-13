"""T9: verdict -> exit code mapping through the CLI run path."""

import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import runner as runner_mod
from .helpers import scenario


class TestVerdictExit(unittest.TestCase):
    def _exit_for(self, verdict):
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            sc_path = Path(tmp) / "scenario.json"
            sc_path.write_text(json.dumps(sc), encoding="utf-8")
            canned = {
                "schemaVersion": 1, "runner": "verification-runner",
                "scenario": {"id": sc["id"], "schemaVersion": 1},
                "verdict": verdict, "reasons": [],
                "evidence": {"complete": True, "missing": [],
                             "resultPath": "out/x.json"},
            }
            with patch("runner.run_loop", return_value=canned):
                code = runner_mod.main(["run", str(sc_path)])
        return code

    def test_exit_codes(self):  # T9
        self.assertEqual(self._exit_for("PASS"), 0)
        self.assertEqual(self._exit_for("FAIL"), 1)
        self.assertEqual(self._exit_for("TIMEOUT"), 2)
        self.assertEqual(self._exit_for("INCOMPLETE"), 3)


if __name__ == "__main__":
    unittest.main()
