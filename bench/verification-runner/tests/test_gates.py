"""T7: checklist gate is MANDATORY for every run (probe included, issue #60
section 0.3); refusal blocks ALL physical operations (probe/flash/capture)."""

import json
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

from runner import run_loop
from .helpers import (bench_alive, flash_verify_scenario, probe_ok,
                      scenario, signed_checklist, unsigned_checklist)


def _patches(probe=None, uart=None):
    probe = probe or probe_ok()
    uart = uart or {"port": "COM9", "open": True, "bytes": 0}
    return [
        patch("tools.probe.stlink_probe", return_value=probe),
        patch("tools.probe.uart_check", return_value=uart),
        patch("tools.flash.flash_firmware",
              return_value={"ok": True, "env": "firmware", "durationS": 1.0}),
        patch("tools.capture.run_capture", return_value=(b"", "raw.bin")),
        patch("tools.identity.git_identity",
              return_value={"gitSha": "abc123", "gitDescribe": "abc123"}),
        patch("tools.identity.artifact_sha256",
              return_value={"artifact": "firmware.bin", "artifactSha256": "x" * 64}),
        patch("tools.identity.toolchain_identity",
              return_value={"platformio": "6.1.19",
                            "platform": "ststm32@17.4.0",
                            "core": "framework-arduinoststm32@4.20701.0"}),
    ]


def enter(patch_list):
    stack = ExitStack()
    return stack, [stack.enter_context(p) for p in patch_list]


class TestGates(unittest.TestCase):
    def test_missing_checklist_blocks_probe_flash_capture(self):  # T7
        # Non-flash (behavior) scenario without sign-off refuses: probe halts
        # the MCU, so it is physical interaction and must be gated.
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            stack, mocks = enter(_patches())
            with stack:
                m_probe, m_uart, m_flash, m_capture = mocks[:4]
                result = run_loop(sc, "COM9", Path(tmp))
        m_probe.assert_not_called()
        m_uart.assert_not_called()
        m_flash.assert_not_called()
        m_capture.assert_not_called()
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("checklist", result["evidence"]["missing"])
        self.assertIn("uartPort", result["evidence"]["missing"])
        self.assertEqual(result["board"]["probe"], "SKIPPED")
        self.assertFalse(result["uart"]["open"])
        self.assertIsNone(result["capture"]["rawPath"])
        self.assertFalse(result["evidence"]["complete"])

    def test_unsigned_checklist_blocks_probe(self):  # T7b
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = unsigned_checklist(tmp)
            stack, mocks = enter(_patches())
            with stack:
                m_probe, m_uart = mocks[:2]
                result = run_loop(sc, "COM9", Path(tmp),
                                  checklist_path=cl_path)
        m_probe.assert_not_called()
        m_uart.assert_not_called()
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("checklistSignoff", result["evidence"]["missing"])

    def test_forged_minimal_checklist_refused(self):  # T7c
        # `{"signed": true}` alone must NOT authorize physical ops: the
        # loaded checklist shape is validated in full
        # (schemaVersion/owner/at/items+confirmed).
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            forged = Path(tmp) / "checklist.json"
            forged.write_text(json.dumps({"signed": True}), encoding="utf-8")
            stack, mocks = enter(_patches())
            with stack:
                m_probe, m_uart, m_flash, m_capture = mocks[:4]
                result = run_loop(sc, "COM9", Path(tmp),
                                  checklist_path=str(forged))
        m_probe.assert_not_called()
        m_uart.assert_not_called()
        m_flash.assert_not_called()
        m_capture.assert_not_called()
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("checklistSchemaVersion",
                      result["evidence"]["missing"])
        # Emitted checklist record conforms to result-v1.json shape
        self.assertEqual(result["checklist"]["schemaVersion"], 1)
        self.assertFalse(result["checklist"]["signed"])

    def test_malformed_checklist_json_refused(self):  # T7d
        # Unreadable checklist JSON must refuse, not escape run_loop.
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            bad = Path(tmp) / "checklist.json"
            bad.write_text("not json {{{", encoding="utf-8")
            stack, mocks = enter(_patches())
            with stack:
                m_probe, m_uart = mocks[:2]
                result = run_loop(sc, "COM9", Path(tmp),
                                  checklist_path=str(bad))
        m_probe.assert_not_called()
        m_uart.assert_not_called()
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("checklistSignoff", result["evidence"]["missing"])

    def test_unconfirmed_items_refused(self):  # T7e
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            cl = Path(tmp) / "checklist.json"
            cl.write_text(json.dumps({
                "schemaVersion": 1, "signed": True, "owner": "Driadix",
                "at": "now",
                "items": [{"text": "bench state attested",
                           "confirmed": False}]}), encoding="utf-8")
            stack, mocks = enter(_patches())
            with stack:
                m_probe = mocks[0]
                result = run_loop(sc, "COM9", Path(tmp),
                                  checklist_path=str(cl))
        m_probe.assert_not_called()
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("checklistItemUnconfirmed",
                      result["evidence"]["missing"])

    def test_non_flash_with_checklist_runs(self):  # uart-probe shape
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = signed_checklist(tmp)
            stack, mocks = enter(_patches())
            with stack:
                m_probe, _m_uart, m_flash, m_capture = mocks[:4]
                m_capture.return_value = (
                    bench_alive(), (Path(tmp) / "raw-t-behavior.bin").as_posix())
                result = run_loop(sc, "COM9", Path(tmp), checklist_path=cl_path)
        m_probe.assert_called_once()
        m_flash.assert_not_called()
        self.assertTrue(result["checklist"]["signed"])
        self.assertEqual(result["verdict"], "PASS")

    def test_flash_requires_checklist_and_flashes(self):  # flash-verify path
        sc = flash_verify_scenario()
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = signed_checklist(tmp)
            stack, mocks = enter(_patches())
            with stack:
                m_flash = mocks[2]
                m_capture = mocks[3]
                m_capture.return_value = (
                    b"", (Path(tmp) / "raw-t-flash-verify.bin").as_posix())
                result = run_loop(sc, "COM9", Path(tmp), checklist_path=cl_path)
        m_flash.assert_called_once()
        self.assertEqual(result["verdict"], "PASS")
        self.assertEqual(result["flash"]["ok"], True)


if __name__ == "__main__":
    unittest.main()
