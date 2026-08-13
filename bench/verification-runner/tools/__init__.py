"""Bench tooling package for the verification runner (ticket #65).

Shared environment resolution: the repo root is derived from this module's
location so external subprocesses (git, PlatformIO, OpenOCD) and artifact
lookups work regardless of the caller's working directory.
"""

import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]


def pio_cmd() -> str:
    """PlatformIO invocation: venv python -m platformio (PIO_CMD override)."""
    return os.environ.get(
        "PIO_CMD",
        str(REPO_ROOT / ".venv-pio312" / "Scripts" / "python.exe") + " -m platformio",
    )
