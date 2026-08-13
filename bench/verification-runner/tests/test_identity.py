"""T8, T13, T14: identity contract - missing firmware identity and bench
identity mismatch are explicit non-passes; mismatch blocks flash and capture.
T14: toolchain_identity records {platformio, platform, core} (issue #60
section 2.3: recording only, pin comparison is the check_toolchain.py CI
contract)."""

import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from runner import run_loop
from tools.identity import toolchain_identity
from .test_gates import _patches, enter
from .helpers import (flash_verify_scenario, scenario, signed_checklist)


class TestIdentity(unittest.TestCase):
    def test_missing_git_sha_and_artifact(self):  # T8
        sc = scenario()
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = signed_checklist(tmp)
            stack, mocks = enter(_patches(
                probe={"probe": "PASS", "part": "STM32F405RG",
                       "idcode": "0x100f6413",
                       "uid": "002900363033470336363131"}))
            with stack:
                mocks[4].return_value = {"gitSha": None, "gitDescribe": None}
                mocks[5].return_value = {"artifact": None, "artifactSha256": None}
                result = run_loop(sc, "COM9", Path(tmp), checklist_path=cl_path)
        missing = result["evidence"]["missing"]
        self.assertIn("gitSha", missing)
        self.assertIn("artifactSha256", missing)
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIsNone(result["capture"]["rawPath"])

    def test_board_identity_mismatch_blocks_flash_capture(self):  # T13
        sc = flash_verify_scenario()
        wrong_uid = {"probe": "PASS", "part": "STM32F405RG",
                     "idcode": "0x100f6413",
                     "uid": "deadbeefdeadbeefdeadbeef"}
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = signed_checklist(tmp)
            stack, mocks = enter(_patches(probe=wrong_uid))
            with stack:
                m_flash = mocks[2]
                m_capture = mocks[3]
                result = run_loop(sc, "COM9", Path(tmp), checklist_path=cl_path)
        m_flash.assert_not_called()
        m_capture.assert_not_called()
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("boardIdentityMismatch", result["evidence"]["missing"])
        self.assertTrue(any("board identity mismatch" in r
                            for r in result["reasons"]))
        # Both observed and expected identities are emitted
        self.assertEqual(result["board"]["uid"], "deadbeefdeadbeefdeadbeef")
        self.assertEqual(result["boardExpected"]["uid"],
                         "002900363033470336363131")
        self.assertTrue(result["flash"]["skipped"])

    def test_board_part_mismatch(self):  # T13b: wrong MCU part
        sc = scenario()
        wrong_part = {"probe": "PASS", "part": "STM32F407VG",
                      "idcode": "0x100f6413",
                      "uid": "002900363033470336363131"}
        with tempfile.TemporaryDirectory() as tmp:
            cl_path = signed_checklist(tmp)
            stack, mocks = enter(_patches(probe=wrong_part))
            with stack:
                result = run_loop(sc, "COM9", Path(tmp), checklist_path=cl_path)
        self.assertEqual(result["verdict"], "INCOMPLETE")
        self.assertIn("boardIdentityMismatch", result["evidence"]["missing"])


class TestToolchainIdentity(unittest.TestCase):
    def test_parses_ststm32_block_only(self):  # T14a
        # Mirrors real `pio pkg list` output: the same package name appears
        # under the native env with a different version; the parser must
        # resolve platform/core inside the 'Platform ststm32' block only.
        pkg_out = (
            "Resolving firmware dependencies...\n"
            "Platform ststm32 @ 17.4.0 (required: ststm32 @ 17.4.0)\n"
            "|-- framework-arduinoststm32 @ 4.20701.0 "
            "(required: platformio/framework-arduinoststm32 @ 4.20701.0)\n"
            "`-- toolchain-gccarmnoneeabi @ 1.120301.0 "
            "(required: platformio/toolchain-gccarmnoneeabi @ 1.120301.0)\n"
            "\n"
            "Resolving native dependencies...\n"
            "Platform native @ 1.2.1 (required: native)\n"
            "`-- framework-arduinoststm32 @ 4.21200.0 "
            "(required: platformio/framework-arduinoststm32)\n"
        )
        with patch("subprocess.run") as m_run:
            m_run.side_effect = [
                SimpleNamespace(stdout="PlatformIO Core, version 6.1.19\n"),
                SimpleNamespace(stdout=pkg_out),
            ]
            got = toolchain_identity()
        self.assertEqual(got, {
            "platformio": "6.1.19",
            "platform": "ststm32@17.4.0",
            "core": "framework-arduinoststm32@4.20701.0",
        })

    def test_unresolvable_fields_record_none(self):  # T14b
        # pio unavailable: both subprocess calls still happen (recording
        # contract), unresolved fields become None, never MagicMock.
        with patch("subprocess.run") as m_run:
            m_run.side_effect = [
                SimpleNamespace(stdout=""),
                SimpleNamespace(stdout=""),
            ]
            got = toolchain_identity()
        self.assertEqual(got, {
            "platformio": None, "platform": None, "core": None,
        })


if __name__ == "__main__":
    unittest.main()
