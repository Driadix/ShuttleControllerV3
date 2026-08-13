"""Bring-up CAN loopback check (ticket #61, D1a). Flashes the bringup-can env,
reads the result struct from RAM via OpenOCD and interprets it.

Exit codes: 0 = PASS, 3 = FAIL (hardware mismatch), 4 = tool error.

Leaves the bringup-can diagnostic on the board; re-flash the production
`firmware` env afterwards (bring-up report procedure).
"""

import os
import subprocess
import sys

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "verification-runner"))

from bringup_logic import CAN_RESULT_ADDR, interpret_can, parse_ram_words  # noqa: E402
from flash_diag import flash_diag  # noqa: E402
from tools.probe import OPENOCD, kill_openocd  # noqa: E402


def read_result(timeout_s=40):
    """Halt and read the 5-word result struct. Raises on failure."""
    kill_openocd()
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
           "-c", "init; halt; mdw 0x%X 5" % CAN_RESULT_ADDR, "-c", "shutdown"]
    r = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    return parse_ram_words((r.stdout or "") + (r.stderr or ""))


def main():
    try:
        flash_diag(env="bringup-can")
        words = read_result()
        result = interpret_can(words)
    except Exception as exc:  # noqa: BLE001 - CLI must not traceback
        print("CAN loopback check error: %s" % exc)
        return 4
    print("CAN loopback result: %s" % result)
    return 0 if result["pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
