"""T4-T6: oracle verdicts (PASS / TIMEOUT / FAIL)."""

import unittest

from runner import evaluate_oracle, normalize_raw
from .helpers import bench_alive, noisy, scenario


class TestOracle(unittest.TestCase):
    def test_pass(self):  # T4: minFrames reached, ratio in range
        verdict, reasons = evaluate_oracle(
            scenario(), normalize_raw(bench_alive()))
        self.assertEqual(verdict, "PASS")
        self.assertEqual(reasons, [])

    def test_timeout_min_frames(self):  # T5
        verdict, reasons = evaluate_oracle(
            scenario(oracle={"minFrames": 23, "maxCrcBadRatio": 1.0,
                             "requirePatterns": [], "forbidPatterns": []}),
            normalize_raw(bench_alive()))
        self.assertEqual(verdict, "TIMEOUT")
        self.assertIn("frames 22 < minFrames 23", reasons[0])

    def test_fail_bad_ratio(self):  # T6a
        verdict, reasons = evaluate_oracle(
            scenario(oracle={"minFrames": 22, "maxCrcBadRatio": 0.2,
                             "requirePatterns": [], "forbidPatterns": []}),
            normalize_raw(noisy()))
        self.assertEqual(verdict, "FAIL")
        self.assertIn("crcBadRatio", reasons[0])

    def test_fail_forbidden_pattern(self):  # T6b
        verdict, reasons = evaluate_oracle(
            scenario(oracle={"minFrames": 22, "maxCrcBadRatio": 1.0,
                             "requirePatterns": [], "forbidPatterns": ["kernel"]}),
            normalize_raw(bench_alive()))
        self.assertEqual(verdict, "FAIL")
        self.assertIn("forbidden pattern matched", reasons[0])

    def test_timeout_required_pattern(self):  # T6c
        verdict, reasons = evaluate_oracle(
            scenario(oracle={"minFrames": 0, "maxCrcBadRatio": 1.0,
                             "requirePatterns": ["not-present"],
                             "forbidPatterns": []}),
            normalize_raw(bench_alive()))
        self.assertEqual(verdict, "TIMEOUT")
        self.assertIn("required pattern not found", reasons[0])

    def test_empty_capture_ratio_guard(self):  # frames==0: no division, no FAIL
        verdict, reasons = evaluate_oracle(
            scenario(oracle={"minFrames": 0, "maxCrcBadRatio": 0.0,
                             "requirePatterns": [], "forbidPatterns": []}),
            normalize_raw(b""))
        self.assertEqual(verdict, "PASS")
        self.assertEqual(reasons, [])


if __name__ == "__main__":
    unittest.main()
