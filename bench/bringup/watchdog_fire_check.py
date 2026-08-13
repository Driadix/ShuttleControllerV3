"""Bring-up watchdog fire check (ticket #61, D2b). Flashes the
bringup-watchdog env (RAM phase marker + IWDG armed, no reload), waits for
the self-reset, then reads the PASSED marker AND RCC_CSR.IWDGRSTF via OpenOCD.

Exit codes: 0 = PASS, 3 = FAIL (watchdog did not fire or marker missing),
4 = tool error.

The experiment is the only way to prove arming + firing on STM32F405: the
IWDG has no readable down-counter or WDGA bit (RM0090 section 22.3). The
marker + flag combination prevents a stale IWDGRSTF (from a prior experiment;
RCC reset flags survive NRST) from false-passing. The bench board carries no
production data, the self-reset is harmless. Leaves the diagnostic on the
board; re-flash the production `firmware` env afterwards (bring-up report
procedure).
"""

import os
import subprocess
import sys
import time

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "verification-runner"))

from bringup_logic import (  # noqa: E402
    RCC_CSR_ADDR,
    WDG_MARKER_ADDR,
    interpret_watchdog,
    parse_mdw_lines,
)
from flash_diag import flash_diag  # noqa: E402
from tools.probe import OPENOCD, kill_openocd  # noqa: E402

# 2 s IWDG window, LSI 17-47 kHz tolerance -> worst case ~3.8 s to fire.
# Wait 6 s before reading (the PASSED marker keeps the board alive after the
# fire, so the read window is not tight).
FIRE_WAIT_S = 6.0


def read_evidence(timeout_s=40):
    """Read the RAM marker (2 words) + RCC_CSR (1 word) in one session."""
    kill_openocd()
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
           "-c", "init; mdw 0x%X 2" % WDG_MARKER_ADDR,
           "-c", "mdw 0x%X 1" % RCC_CSR_ADDR, "-c", "shutdown"]
    r = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    lines = parse_mdw_lines((r.stdout or "") + (r.stderr or ""))
    marker = lines.get(WDG_MARKER_ADDR)
    csr = lines.get(RCC_CSR_ADDR)
    if marker is None or csr is None:
        raise RuntimeError("mdw lines missing (marker=%r csr=%r): %s"
                           % (marker, csr, ((r.stdout or "") + (r.stderr or ""))[-400:]))
    return csr[0], marker


def main():
    try:
        flash_diag(env="bringup-watchdog",
                   zero_addrs=[WDG_MARKER_ADDR, WDG_MARKER_ADDR + 4])
        print("waiting %.0f s for IWDG self-reset..." % FIRE_WAIT_S)
        time.sleep(FIRE_WAIT_S)
        csr, marker = read_evidence()
        result = interpret_watchdog(csr, marker)
    except Exception as exc:  # noqa: BLE001 - CLI must not traceback
        print("watchdog fire check error: %s" % exc)
        return 4
    print("watchdog fire result: %s" % result)
    return 0 if result["pass"] else 3


if __name__ == "__main__":
    sys.exit(main())
