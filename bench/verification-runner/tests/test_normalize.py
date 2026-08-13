"""T1-T3: frame normalization (valid / resync / bad CRC)."""

import unittest

from runner import normalize_raw
from .helpers import bench_alive, noisy


class TestNormalize(unittest.TestCase):
    def test_valid_frames(self):  # T1
        raw = bench_alive()
        norm = normalize_raw(raw)
        self.assertEqual(norm["bytes"], len(raw))
        self.assertEqual(norm["framesValid"], 22)
        self.assertEqual(norm["framesBad"], 0)
        self.assertEqual(norm["msgCounts"], {
            "MSG_SENSORS": 10, "MSG_AS5600_HEALTH": 10, "MSG_LOG": 2})
        self.assertEqual(norm["logLines"],
                         ["boot: Init encoder success", "kernel: ready"])

    def test_resync(self):  # T2: garbage between frames + truncated tail
        alive = bench_alive()
        # Stray 0xBB (not followed by 0xCC) and a false frame start with
        # length > MAX_PAYLOAD: parser must resync without losing frames and
        # without creating false frames at the segment boundaries.
        garbage = b"\xBB\x67" * 2 + b"\xBB\xCC\x10\x00\x01\xBB\x67"
        raw = garbage + alive + garbage + alive[:30]  # tail: 1 complete frame
        norm = normalize_raw(raw)
        self.assertEqual(norm["framesValid"], 23)
        self.assertEqual(norm["framesBad"], 0)

    def test_bad_crc(self):  # T3
        norm = normalize_raw(noisy())
        self.assertEqual(norm["framesValid"], 22)
        self.assertEqual(norm["framesBad"], 12)
        self.assertEqual(norm["msgCounts"]["MSG_LOG"], 14)  # 2 valid + 12 bad


if __name__ == "__main__":
    unittest.main()
