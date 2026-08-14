"""Host tests for the sensing prototype interpreter (ticket #63). Run with
the bench venv: ../../.venv-pio312/Scripts/python.exe -m unittest discover
-s bench/sensing-proto/tests -t .
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import sensing_logic as sl  # noqa: E402


def build_words(uptime_ms=5000, tof_ok=(150, 0, 0, 150), as5600_ok=20,
                tof_fail=(0, 20, 20, 0), age=(5, 5, 5, 5, 30),
                tof_read_us_max=1500, tof_fresh_ok=True):
    """Construct the 52 diagnostic words for a snapshot."""
    words = [sl.MAGIC, sl.VERSION, uptime_ms, 100, 1000, 40,
             4900, 4900, tof_read_us_max, 80000, 100, 0]
    for i in range(4):
        ok = tof_ok[i]
        fail = tof_fail[i]
        present = i in (0, 3)
        # Firmware: 3 consecutive failures -> Faulted even without a first
        # success; fewer failures with no success -> Starting.
        state = 3 if (not present and fail >= 3) else (1 if present else 0)
        if present and fail == 0:
            state = 1  # Healthy
        age_ms = age[i] if (present and tof_fresh_ok) else 0xFFFFFFFF
        words += [1000 + i, 42, age_ms, state, ok, fail, 0 if present else 1,
                  uptime_ms - 5]
    words += [1234, 4321, age[4], 1, as5600_ok, 0, 0, uptime_ms - 30]
    return words


def mdw_text(words, base=sl.DIAG_ADDR, per_line=16):
    """Format words as OpenOCD mdw output lines."""
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i:i + per_line]
        addr = base + i * 4
        lines.append("0x%08X: %s" % (addr, " ".join("%08X" % w for w in chunk)))
    return "\n".join(lines) + "\n"


class ParseTest(unittest.TestCase):
    def test_parse_mdw_lines(self):
        text = "0x20011000: 00000001 00000002\n0x20011008: 00000003\n"
        parsed = sl.parse_mdw_lines(text)
        self.assertEqual(parsed[0x20011000], [1, 2])
        self.assertEqual(parsed[0x20011008], [3])

    def test_parse_mdw_lines_empty(self):
        with self.assertRaises(ValueError):
            sl.parse_mdw_lines("no data here")

    def test_words_from_mdw_contiguous(self):
        words = build_words()
        text = mdw_text(words)
        out = sl.words_from_mdw(text)
        self.assertEqual(out, words)

    def test_words_too_few(self):
        text = mdw_text([1, 2, 3])
        with self.assertRaises(ValueError):
            sl.words_from_mdw(text)


class SnapshotTest(unittest.TestCase):
    def test_interpret_valid(self):
        snap = sl.interpret_snapshot(build_words())
        self.assertTrue(snap["valid"])
        self.assertEqual(len(snap["sensors"]), 5)
        self.assertEqual(snap["sensors"][0]["name"], "tof_channel_reverse")
        self.assertEqual(snap["sensors"][4]["name"], "as5600")

    def test_interpret_bad_magic(self):
        words = build_words()
        words[0] = 0xDEADBEEF
        snap = sl.interpret_snapshot(words)
        self.assertFalse(snap["valid"])
        self.assertIn("magic", snap["error"])

    def test_interpret_short(self):
        snap = sl.interpret_snapshot([1, 2, 3])
        self.assertFalse(snap["valid"])


class VerdictTest(unittest.TestCase):
    def test_cadence_pass(self):
        a = sl.interpret_snapshot(build_words(uptime_ms=5000))
        b = sl.interpret_snapshot(build_words(uptime_ms=10000, tof_ok=(300, 0, 0, 300),
                                              as5600_ok=40, tof_fail=(0, 40, 40, 0)))
        v = sl.cadence_verdict(a, b, window_s=5.0)
        self.assertTrue(v["pass"], v)
        self.assertEqual(v["checks"][0]["delta"], 150)

    def test_cadence_fail(self):
        a = sl.interpret_snapshot(build_words(uptime_ms=5000))
        b = sl.interpret_snapshot(build_words(uptime_ms=10000, tof_ok=(160, 0, 0, 160)))
        v = sl.cadence_verdict(a, b, window_s=5.0)
        self.assertFalse(v["pass"])
        self.assertFalse(v["checks"][0]["pass"])

    def test_states_present_absent(self):
        snap = sl.interpret_snapshot(build_words())
        v = sl.state_verdict(snap)
        self.assertTrue(v["pass"], v)
        self.assertEqual(v["checks"][0]["observed"], "Healthy")
        self.assertEqual(v["checks"][1]["observed"], "Faulted")

    def test_states_starting_absent_fails(self):
        # Absent sensor with fewer than 3 failures (no sample yet): Starting,
        # which is not the expected Faulted -> fail.
        snap = sl.interpret_snapshot(build_words(tof_fail=(0, 2, 2, 0)))
        v = sl.state_verdict(snap)
        self.assertFalse(v["pass"])
        self.assertEqual(v["checks"][1]["observed"], "Starting")

    def test_freshness_pass(self):
        snap = sl.interpret_snapshot(build_words())
        v = sl.freshness_verdict(snap)
        self.assertTrue(v["pass"], v)

    def test_freshness_stale_fails(self):
        snap = sl.interpret_snapshot(build_words(age=(500, 5, 5, 5, 30)))
        v = sl.freshness_verdict(snap)
        self.assertFalse(v["pass"])
        self.assertEqual(v["checks"][0]["age_ms"], 500)

    def test_budget_pass_and_fail(self):
        snap = sl.interpret_snapshot(build_words(tof_read_us_max=1500))
        self.assertTrue(sl.budget_verdict(snap)["pass"])
        snap2 = sl.interpret_snapshot(build_words(tof_read_us_max=9000))
        self.assertFalse(sl.budget_verdict(snap2)["pass"])

    def test_full_verdict_pass(self):
        a = sl.interpret_snapshot(build_words(uptime_ms=5000))
        b = sl.interpret_snapshot(build_words(uptime_ms=10000, tof_ok=(300, 0, 0, 300),
                                              as5600_ok=40, tof_fail=(0, 40, 40, 0)))
        v = sl.full_verdict(a, b, 5.0)
        self.assertTrue(v["pass"])
        self.assertEqual(v["verdict"], "PASS")

    def test_full_verdict_fail(self):
        a = sl.interpret_snapshot(build_words(uptime_ms=5000))
        b = sl.interpret_snapshot(build_words(uptime_ms=10000, tof_ok=(160, 0, 0, 160)))
        v = sl.full_verdict(a, b, 5.0)
        self.assertFalse(v["pass"])
        self.assertEqual(v["verdict"], "FAIL")


if __name__ == "__main__":
    unittest.main()
