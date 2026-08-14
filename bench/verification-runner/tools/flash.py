"""ST-Link flash via PlatformIO + OpenOCD (gated physical operation, ticket
#65). The firmware carries a pinned runtime-RAM diagnostic section
(.bram_sensing, sensing slice #63 HITL §0.3): it is stripped from the flash
image (objcopy, the bench/bringup/flash_diag.py pattern) before programming,
because a loadable RAM segment in the ELF makes the standard upload verify
fail. Envs without the section are flashed identically (the strip is a
no-op warning).
"""

import os
import shlex
import subprocess
import time

from tools import pio_cmd, REPO_ROOT
from tools.probe import OPENOCD, kill_openocd

OBJCOPY = os.path.join(
    os.environ.get("USERPROFILE", ""),
    ".platformio", "packages", "toolchain-gccarmnoneeabi", "bin",
    "arm-none-eabi-objcopy.exe",
)
FLASH_BASE = 0x08000000
# Pinned runtime-RAM diagnostic sections (loadable in the ELF; stripped from the
# flash image - a loadable RAM segment makes the standard upload verify fail).
RAM_SECTIONS = [".bram_sensing", ".bram_safety"]


def flash_firmware(env: str = "firmware", timeout_s: int = 600) -> dict:
    """Build, strip the RAM diagnostic section, program via OpenOCD
    `reset halt` + `reset run`. Returns {ok, env, durationS} or raises."""
    pio = shlex.split(pio_cmd(), posix=False)
    t0 = time.time()
    kill_openocd()
    r = subprocess.run(pio + ["run", "-e", env], capture_output=True,
                       text=True, timeout=timeout_s, cwd=REPO_ROOT)
    if r.returncode != 0:
        raise RuntimeError(f"build failed: "
                           f"{(r.stdout or '')[-500:]}{(r.stderr or '')[-500:]}")

    elf = REPO_ROOT / ".pio" / "build" / env / "firmware.elf"
    out = REPO_ROOT / ".pio" / "build" / env / "firmware-flash.bin"
    args = [OBJCOPY, "-O", "binary"]
    for sec in RAM_SECTIONS:
        args += ["--remove-section", sec]
    args += [str(elf), str(out)]
    r2 = subprocess.run(args, capture_output=True, text=True,
                        timeout=timeout_s, cwd=REPO_ROOT)
    if r2.returncode != 0 or not out.exists():
        raise RuntimeError(f"objcopy failed: "
                           f"{(r2.stdout or '')[-500:]}{(r2.stderr or '')[-500:]}")

    cmds = ["init; reset halt",
            "program %s 0x%X verify" % (out.as_posix(), FLASH_BASE),
            "mww 0x20011000 0x0",  # stale .bram_sensing marker must not read
            # Clear the whole .bram_safety diag (122 words): stale counters/ring must
            # not read (L4 eq/delta rules). `fill` is unavailable in this OpenOCD build.
            "; ".join("mww 0x%X 0x0" % (0x20012000 + 4 * i) for i in range(122)),
            "reset run",
            "shutdown"]
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg"]
    for c in cmds:
        cfg += ["-c", c]
    r3 = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    o = (r3.stdout or "") + (r3.stderr or "")
    if r3.returncode != 0:
        raise RuntimeError(f"upload failed: {o[-500:]}")
    return {"ok": True, "env": env, "durationS": round(time.time() - t0, 1)}
