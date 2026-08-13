"""RAM read-back over OpenOCD (verification runner, scenario v2, ticket #63).
Reads raw words at a pinned address (diagnostic struct of the firmware under
test) with `resume` after the snapshot so the probe keeps running - the same
pattern as bench/sensing-proto (ticket #63 prototype) and bring-up #61.
"""

import os
import re
import subprocess

from tools.probe import OPENOCD, kill_openocd

_MDW_LINE_RE = re.compile(r"0x([0-9a-fA-F]{8}):\s*((?:[0-9a-fA-F]{8}\s*)+)")


def read_ram_words(addr: int, count: int, timeout_s: int = 60) -> list:
    """Halt, read `count` 32-bit words at `addr`, resume, shutdown. Returns
    the words in address order. Raises on failure (no mdw output)."""
    kill_openocd()
    cfg = [OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
           "-c", "init; halt; mdw 0x%X %d" % (addr, count),
           "-c", "resume",
           "-c", "shutdown"]
    r = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    out = (r.stdout or "") + (r.stderr or "")
    words = []
    for m in _MDW_LINE_RE.finditer(out):
        base = int(m.group(1), 16)
        for i, w in enumerate(m.group(2).split()):
            if base + i * 4 >= addr:
                words.append(int(w, 16))
    if len(words) < count:
        raise RuntimeError(
            "readback failed: expected %d words, got %d: %s"
            % (count, len(words), out[-300:]))
    return words[:count]
