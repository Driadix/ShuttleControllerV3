"""T23: v2 readback observation is gated like probe/flash - a refused run
(no checklist) must NOT call read_ram_words (it halts the MCU: physical
interaction, issue #60 section 0.3)."""

import json
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

from runner import run_loop
from .helpers import probe_ok
from .test_readback import v2_scenario


def _patches(probe=None):
    probe = probe or probe_ok()
    return [
        patch("runner.probe.stlink_probe", return_value=probe),
        patch("runner.flash.flash_firmware",
              return_value={"ok": True, "env": "firmware"}),
        patch("runner.readback.read_ram_words",
              side_effect=AssertionError("read_ram_words must not be called "
                                         "on a refused run")),
    ]


def _enter(patches):
    stack = ExitStack()
    return stack, [stack.enter_context(p) for p in patches]


class TestReadbackGates(unittest.TestCase):
    def test_missing_checklist_blocks_readback(self):  # T23
        sc = v2_scenario()
        stack, _ = _enter(_patches())
        with stack, tempfile.TemporaryDirectory() as tmp:
            result = run_loop(sc, "COM9", Path(tmp))
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("checklist", result["evidence"]["missing"])
        self.assertIs(result["capture"].get("skipped"), True)
        # read_ram_words was never reached (side_effect would have raised).

    def test_board_mismatch_blocks_readback(self):  # T23b
        sc = v2_scenario()
        probe = probe_ok()
        probe["uid"] = "f" * 24  # mismatched 96-bit UID
        stack, _ = _enter(_patches(probe=probe))
        with stack, tempfile.TemporaryDirectory() as tmp:
            checklist = Path(tmp) / "checklist.json"
            checklist.write_text(
                json.dumps({
                    "schemaVersion": 1, "signed": True, "owner": "Driadix",
                    "at": "2026-08-13T00:00:00Z",
                    "items": [{"text": "ok", "confirmed": True}],
                }, ensure_ascii=False), encoding="utf-8")
            result = run_loop(sc, "COM9", Path(tmp), checklist_path=str(checklist))
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("boardIdentityMismatch", result["evidence"]["missing"])
        self.assertIs(result["capture"].get("skipped"), True)


if __name__ == "__main__":
    unittest.main()
