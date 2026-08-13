"""ST-Link flash via PlatformIO (gated physical operation, ticket #65)."""

import shlex
import subprocess
import time

from tools import pio_cmd, REPO_ROOT
from tools.probe import kill_openocd


def flash_firmware(env: str = "firmware", timeout_s: int = 600) -> dict:
    """Build + upload via ST-Link. Returns {ok, env, durationS} or raises."""
    pio = shlex.split(pio_cmd(), posix=False)
    t0 = time.time()
    kill_openocd()
    r = subprocess.run(pio + ["run", "-e", env], capture_output=True,
                       text=True, timeout=timeout_s, cwd=REPO_ROOT)
    if r.returncode != 0:
        raise RuntimeError(f"build failed: "
                           f"{(r.stdout or '')[-500:]}{(r.stderr or '')[-500:]}")
    r2 = subprocess.run(pio + ["run", "-e", env, "-t", "upload"],
                        capture_output=True, text=True, timeout=timeout_s,
                        cwd=REPO_ROOT)
    if r2.returncode != 0:
        raise RuntimeError(f"upload failed: "
                           f"{(r2.stdout or '')[-500:]}{(r2.stderr or '')[-500:]}")
    return {"ok": True, "env": env, "durationS": round(time.time() - t0, 1)}
