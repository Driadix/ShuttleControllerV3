"""T1-T3: frame normalization (valid / resync / bad CRC). T20: canonical V3
frame format (observability-uart scenario, #72)."""

import struct
import unittest

from runner import normalize_raw
from .helpers import bench_alive, noisy


def v3_frame(payload: bytes, msg_type: int = 0) -> bytes:
    """Builds one canonical V3 frame (sync 0xE3 0x10 + header 8 + payload +
    CRC-16/CCITT-FALSE), mirroring domain/codec.h."""
    header = bytes((1, 6, msg_type, 3, 0, 0)) + struct.pack("<H", len(payload))
    crc = 0xFFFF
    body = header + payload
    for b in body:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return b"\xE3\x10" + body + struct.pack("<H", crc)


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

    def test_v3_valid_frames(self):  # T20a (#72): canonical V3 decode
        raw = v3_frame(b"\x00" * 16, msg_type=0)  # TelemetryRecord
        raw += v3_frame(b"\x01" * 20, msg_type=1)  # EventRecord
        norm = normalize_raw(raw, frame_format="v3")
        self.assertEqual(norm["framesValid"], 2)
        self.assertEqual(norm["framesBad"], 0)
        self.assertEqual(norm["msgCounts"],
                         {"TelemetryRecord": 1, "EventRecord": 1})

    def test_v3_bad_crc(self):  # T20b (#72): CRC-validity enforced
        raw = v3_frame(b"\x00" * 16)
        corrupted = raw[:-1] + bytes((raw[-1] ^ 0xFF,))
        norm = normalize_raw(corrupted, frame_format="v3")
        self.assertEqual(norm["framesValid"], 0)
        self.assertEqual(norm["framesBad"], 1)

    def test_v3_mixed_garbage(self):  # T20c (#72): lenient resync, no false frames
        raw = b"\xE3\x67" * 3 + v3_frame(b"\x00" * 16) + b"\xE3" + v3_frame(b"\x01" * 8)
        norm = normalize_raw(raw, frame_format="v3")
        self.assertEqual(norm["framesValid"], 2)
        self.assertEqual(norm["framesBad"], 0)


if __name__ == "__main__":
    unittest.main()
