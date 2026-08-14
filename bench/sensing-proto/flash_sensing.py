"""Build + flash the sensing prototype env via OpenOCD reset-halt (ticket
#63, bench/sensing-proto/). Same pattern as bench/bringup/flash_diag.py
(ticket #61): the standard PlatformIO upload fails because the ELF carries
a loadable RAM segment (pinned .bram_sensing marker section); the marker is
stripped from the flash image with objcopy before programming, and zeroed
while halted before `reset run` so a stale marker from an interrupted run
cannot be mistaken for a live snapshot.
"""

import os
import shlex
import subprocess

from tools import REPO_ROOT, pio_cmd
from tools.probe import OPENOCD, kill_openocd

ENV = "sensing-proto"
MARKER_SECTION = ".bram_sensing"
MARKER_ADDR = 0x20011000
FLASH_BASE = 0x08000000

OBJCOPY = os.path.join(
    os.environ.get("USERPROFILE", ""),
    ".platformio", "packages", "toolchain-gccarmnoneeabi", "bin",
    "arm-none-eabi-objcopy.exe",
)


def flash_sensing(env=ENV, timeout_s=600):
    """Build, strip the marker section, program via OpenOCD `reset halt`,
    zero the marker RAM while halted, `reset run`. Raises on failure;
    returns {"ok": True, "env": env}."""
    pio = shlex.split(pio_cmd(), posix=False)
    r = subprocess.run(pio + ["run", "-e", env], capture_output=True,
                       text=True, timeout=timeout_s, cwd=REPO_ROOT)
    if r.returncode != 0:
        raise RuntimeError("build failed: %s%s"
                           % ((r.stdout or "")[-500:], (r.stderr or "")[-500:]))

    elf = REPO_ROOT / ".pio" / "build" / env / "firmware.elf"
    out = REPO_ROOT / ".pio" / "build" / env / "firmware-flash.bin"
    args = [OBJCOPY, "-O", "binary", "--remove-section", MARKER_SECTION,
            str(elf), str(out)]
    r2 = subprocess.run(args, capture_output=True, text=True,
                        timeout=timeout_s, cwd=REPO_ROOT)
    if r2.returncode != 0 or not out.exists():
        raise RuntimeError("objcopy failed: %s%s"
                           % ((r2.stdout or "")[-500:], (r2.stderr or "")[-500:]))

    kill_openocd()
    cmds = ["init; reset halt",
            "program %s 0x%X verify" % (out.as_posix(), FLASH_BASE),
            "mww 0x%X 0x0" % MARKER_ADDR,
            "reset run",
            "shutdown"]
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg"]
    for c in cmds:
        cfg += ["-c", c]
    r3 = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    o = (r3.stdout or "") + (r3.stderr or "")
    if r3.returncode != 0:
        raise RuntimeError("upload failed: %s" % o[-500:])
    return {"ok": True, "env": env}


def readback_words(addr, count, timeout_s=60):
    """Read `count` 32-bit words at `addr` over OpenOCD; returns the raw
    output text (parsed by sensing_logic). `resume` after the mdw so the
    probe keeps running (halt only for the snapshot). Raises on failure."""
    kill_openocd()
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
           "-c", "init; halt; mdw 0x%X %d" % (addr, count),
           "-c", "resume",
           "-c", "shutdown"]
    r = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    out = (r.stdout or "") + (r.stderr or "")
    if r.returncode != 0:
        raise RuntimeError("readback failed: %s" % out[-400:])
    return out
