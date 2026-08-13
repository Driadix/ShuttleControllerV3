"""T10: evidence bundle - complete run writes raw + result, bundle re-check."""

import tempfile
import unittest
from pathlib import Path

from runner import check_evidence, run_loop
from .test_gates import _patches, enter
from .helpers import bench_alive, scenario, signed_checklist


class TestBundle(unittest.TestCase):
    def test_complete_bundle(self):  # T10
        sc = scenario()
        raw = bench_alive()
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = signed_checklist(tmp)
            raw_path = (Path(tmp) / "raw-t-behavior.bin").as_posix()
            Path(raw_path).write_bytes(raw)  # real run_capture side effect
            stack, mocks = enter(_patches())
            with stack:
                m_capture = mocks[3]
                m_capture.return_value = (raw, raw_path)
                result = run_loop(sc, "COM9", Path(tmp), checklist_path=cl_path)
            self.assertTrue(Path(raw_path).exists())
            self.assertTrue((Path(tmp) / "result-t-behavior.json").exists())
            # Bundle re-check via the `evidence` contract
            missing = check_evidence(result, Path(tmp))
            self.assertEqual(missing, [])
        self.assertEqual(result["verdict"], "PASS")
        self.assertTrue(result["evidence"]["complete"])
        self.assertEqual(result["evidence"]["missing"], [])
        self.assertEqual(result["capture"]["rawBytes"], len(raw))

    def test_incomplete_bundle_evidence_check(self):  # refusal result re-check
        result = {
            "schemaVersion": 1, "runner": "verification-runner",
            "scenario": {"id": "t-x", "schemaVersion": 1},
            "verdict": "INCOMPLETE", "reasons": [],
            "normalized": {"bytes": 0, "framesValid": 0, "framesBad": 0,
                           "msgCounts": {}, "logLines": []},
            "evidence": {"complete": False, "missing": ["checklist"],
                         "resultPath": "out/t-x/result-t-x.json"},
        }
        with tempfile.TemporaryDirectory() as tmp:
            missing = check_evidence(result, Path(tmp))
        self.assertIn("evidenceComplete", missing)
        self.assertIn("rawArtifact:raw-t-x.bin", missing)


if __name__ == "__main__":
    unittest.main()
