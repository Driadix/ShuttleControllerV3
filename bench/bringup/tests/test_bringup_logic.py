"""Host tests for bring-up interpretation logic (ticket #61). Pure logic only
- no bench, no hardware. Run: python -m unittest discover -s tests -t .
(from bench/bringup).
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from bringup_logic import (  # noqa: E402
    WDG_MAGIC,
    WDG_PHASE_PASSED,
    interpret_can,
    interpret_watchdog,
    parse_mdw_lines,
    parse_ram_words,
)

PASSED_MARKER = [WDG_MAGIC, WDG_PHASE_PASSED]


class TestParseRamWords(unittest.TestCase):
    def test_mdw_line_with_address_prefix(self):
        text = ("target halted\n"
                "0x20010000: ca11d1a6 00000008 00000008 00000000 00000001\n"
                "shutdown")
        self.assertEqual(parse_ram_words(text), [0xCA11D1A6, 8, 8, 0, 1])

    def test_uppercase_hex_words(self):
        text = "0x20010000: CA11D1A6 00000008 00000008 00000000 00000001"
        self.assertEqual(parse_ram_words(text), [0xCA11D1A6, 8, 8, 0, 1])

    def test_missing_line_raises(self):
        with self.assertRaises(ValueError):
            parse_ram_words("no data here")


class TestParseMdwLines(unittest.TestCase):
    def test_two_lines(self):
        text = ("0x2000f000: 5a5a5a5a 00005eed\n"
                "0x40023874: 20000000\n")
        lines = parse_mdw_lines(text)
        self.assertEqual(lines[0x2000F000], [WDG_MAGIC, WDG_PHASE_PASSED])
        self.assertEqual(lines[0x40023874], [0x20000000])

    def test_missing_raises(self):
        with self.assertRaises(ValueError):
            parse_mdw_lines("nothing here")


class TestInterpretCan(unittest.TestCase):
    def test_pass(self):
        self.assertTrue(interpret_can([0xCA11D1A6, 8, 8, 0, 1])["pass"])

    def test_magic_mismatch_fails(self):
        self.assertFalse(interpret_can([0xDEADBEEF, 8, 8, 0, 1])["pass"])

    def test_rx_missing_fails(self):
        self.assertFalse(interpret_can([0xCA11D1A6, 8, 7, 0, 1])["pass"])

    def test_crc_error_fails(self):
        self.assertFalse(interpret_can([0xCA11D1A6, 8, 8, 1, 1])["pass"])

    def test_not_done_fails(self):
        self.assertFalse(interpret_can([0xCA11D1A6, 8, 8, 0, 0])["pass"])

    def test_wrong_word_count_fails(self):
        result = interpret_can([1, 2, 3])
        self.assertFalse(result["pass"])
        self.assertIn("error", result)


class TestInterpretWatchdog(unittest.TestCase):
    def test_passed_marker_and_flag(self):
        result = interpret_watchdog(0x20000000, PASSED_MARKER)
        self.assertTrue(result["pass"])
        self.assertTrue(result["iwdgrstf"])
        self.assertTrue(result["marker_ok"])

    def test_flag_alone_fails_stale(self):
        # Stale IWDGRSTF from a previous experiment, no PASSED marker.
        self.assertFalse(interpret_watchdog(0x20000000, [0, 0])["pass"])

    def test_marker_alone_fails(self):
        # PASSED marker but flag cleared - cannot have happened in this run.
        self.assertFalse(interpret_watchdog(0x0, PASSED_MARKER)["pass"])

    def test_foreign_ram_content_fails(self):
        self.assertFalse(interpret_watchdog(0x20000000, [0xDEADBEEF, 0x1234])["pass"])

    def test_short_marker_fails(self):
        self.assertFalse(interpret_watchdog(0x20000000, [WDG_MAGIC])["pass"])

    def test_fired_with_other_flags(self):
        # PINRSTF + IWDGRSTF together still count as fired.
        self.assertTrue(interpret_watchdog(0xA0000000, PASSED_MARKER)["pass"])


if __name__ == "__main__":
    unittest.main()
