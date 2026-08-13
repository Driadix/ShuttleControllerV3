"""Build + flash a bring-up env via OpenOCD reset-halt (ticket #61).

The standard PlatformIO upload fails on these diags because the ELF carries
a loadable RAM segment (pinned marker sections): the raw objcopy image is
inflated up to the RAM address and OpenOCD cannot program/verify a RAM
address into flash ("Verify Failed"). The marker sections are stripped from
the flash image with objcopy before programming - the markers stay at their
pinned RAM addresses on the device (startup never touches custom sections),
they are just not part of the flash payload.

`reset halt` before programming, plus optional `zero_addrs` (mww zero while
halted) and a final `reset run`, give a deterministic marker state on every
run: RAM survives programming and resets, so without the zeroing a stale
marker from an interrupted run could skip the arm branch.
"""

import os
import shlex
import subprocess

from tools import REPO_ROOT, pio_cmd
from tools.probe import OPENOCD, kill_openocd

# F405RG flash base; LD_FLASH_OFFSET=0x0 in the frozen build flags.
FLASH_BASE = 0x08000000

OBJCOPY = os.path.join(
    os.environ.get("USERPROFILE", ""),
    ".platformio", "packages", "toolchain-gccarmnoneeabi", "bin",
    "arm-none-eabi-objcopy.exe",
)

# Marker sections stripped from the flash image (kept on device RAM).
_MARKER_SECTIONS = [".bram_diag", ".bram_wdg"]


def _flash_image(env, timeout_s=120):
    """objcopy -O binary with marker sections removed -> firmware-flash.bin."""
    elf = REPO_ROOT / ".pio" / "build" / env / "firmware.elf"
    out = REPO_ROOT / ".pio" / "build" / env / "firmware-flash.bin"
    args = [OBJCOPY, "-O", "binary"]
    for sec in _MARKER_SECTIONS:
        args += ["--remove-section", sec]
    r = subprocess.run(args + [str(elf), str(out)], capture_output=True,
                       text=True, timeout=timeout_s, cwd=REPO_ROOT)
    if r.returncode != 0 or not out.exists():
        raise RuntimeError("objcopy failed: %s%s"
                           % ((r.stdout or "")[-500:], (r.stderr or "")[-500:]))
    return out


def flash_diag(env, timeout_s=600, zero_addrs=()):
    """Build (pio run), strip marker sections, program via OpenOCD
    `reset halt`. With `zero_addrs`, the words are zeroed while the target is
    halted AFTER programming and BEFORE `reset run` - the watchdog diag boots
    from a guaranteed fresh marker state (RAM survives programming and
    resets, so a stale [magic, ARMED] from an interrupted run could otherwise
    skip the arm branch and false-pass). Raises on failure; returns
    {"ok": True, "env": env}."""
    pio = shlex.split(pio_cmd(), posix=False)
    r = subprocess.run(pio + ["run", "-e", env], capture_output=True,
                       text=True, timeout=timeout_s, cwd=REPO_ROOT)
    if r.returncode != 0:
        raise RuntimeError("build failed: %s%s"
                           % ((r.stdout or "")[-500:], (r.stderr or "")[-500:]))
    bin_path = _flash_image(env)
    kill_openocd()
    cmds = ["init; reset halt",
            "program %s 0x%X verify" % (bin_path.as_posix(), FLASH_BASE)]
    for addr in zero_addrs:
        cmds.append("mww 0x%X 0x0" % addr)
    cmds.append("reset run")
    cmds.append("shutdown")
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg"]
    for c in cmds:
        cfg += ["-c", c]
    r2 = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    out = (r2.stdout or "") + (r2.stderr or "")
    if r2.returncode != 0:
        raise RuntimeError("upload failed: %s" % out[-500:])
    return {"ok": True, "env": env}
