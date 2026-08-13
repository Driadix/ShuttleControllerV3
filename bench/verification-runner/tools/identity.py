"""Identity records: firmware (git), artifact sha256, toolchain (ticket #65).

All lookups resolve against the repo root regardless of the caller's working
directory (tools/__init__.py REPO_ROOT).
"""

import datetime as _dt
import hashlib
import re
import shlex
import subprocess

from tools import pio_cmd, REPO_ROOT

_ARTIFACT_DIR = REPO_ROOT / ".pio" / "build" / "firmware"

_VERSION_RE = re.compile(r"@\s*([0-9A-Za-z.+-]+)")


def utcnow() -> str:
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds")


def git_identity() -> dict:
    sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                         text=True, cwd=REPO_ROOT).stdout.strip()
    desc = subprocess.run(["git", "describe", "--always", "--dirty"],
                          capture_output=True, text=True,
                          cwd=REPO_ROOT).stdout.strip()
    return {"gitSha": sha or None, "gitDescribe": desc or None}


def artifact_sha256() -> dict:
    for name in ("firmware.bin", "firmware.hex", "firmware.elf"):
        p = _ARTIFACT_DIR / name
        if p.exists():
            return {"artifact": name,
                    "artifactSha256": hashlib.sha256(p.read_bytes()).hexdigest()}
    return {"artifact": None, "artifactSha256": None}


def toolchain_identity() -> dict:
    """Record {platformio, platform, core} observed toolchain versions.

    platform/core come from the frozen ststm32 platform packages (#51
    section 3). The same package name can be installed under other platforms
    with different versions (native env), so `pio pkg list` is parsed only
    inside the 'Platform ststm32' block. Recording only, per #60 section 2.3:
    pin comparison against #51 section 3 is the separate CI contract
    tools/check_toolchain.py, not the runner. Any unresolved field becomes
    None and is recorded as such.
    """
    pio = shlex.split(pio_cmd(), posix=False)
    ver_out = subprocess.run(pio + ["--version"], capture_output=True,
                             text=True, cwd=REPO_ROOT).stdout
    m = re.search(r"version\s+([0-9A-Za-z.+-]+)", ver_out)
    platformio = m.group(1) if m else None
    pkg_out = subprocess.run(pio + ["pkg", "list"], capture_output=True,
                             text=True, cwd=REPO_ROOT).stdout
    platform = core = None
    in_stm32 = False
    for line in pkg_out.splitlines():
        s = line.strip()
        if s.startswith("Platform ststm32"):
            in_stm32 = True
            m = _VERSION_RE.search(s)
            platform = m.group(1) if m else None
        elif s.startswith("Platform ") or s.startswith("Resolving "):
            in_stm32 = False
        elif in_stm32 and core is None and "framework-arduinoststm32" in s:
            m = _VERSION_RE.search(s)
            if m:
                core = m.group(1)
    return {
        "platformio": platformio,
        "platform": f"ststm32@{platform}" if platform else None,
        "core": f"framework-arduinoststm32@{core}" if core else None,
    }
