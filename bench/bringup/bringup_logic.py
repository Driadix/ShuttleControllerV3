"""Pure interpretation logic for bring-up checks (ticket #61, design
docs/bringup-design-v3.md). Kept free of hardware/CLI imports so the host
tests run without a bench.
"""

import re

# CAN loopback result struct (bringup/can_loopback.cpp), pinned RAM address.
CAN_RESULT_ADDR = 0x20010000
CAN_EXPECTED = {"magic": 0xCA11D1A6, "tx_ok": 8, "rx_ok": 8, "crc_err": 0, "done": 1}

# RCC_CSR (RM0090 section 6.3.18): offset 0x74 from RCC base 0x40023800.
# Bit 29 = IWDGRSTF (independent watchdog reset flag).
RCC_CSR_ADDR = 0x40023874
IWDGRSTF_MASK = 0x20000000

# Watchdog fire experiment RAM phase marker (bringup/watchdog_fire.cpp),
# pinned at 0x2000F000. The host requires the PASSED phase AND IWDGRSTF so a
# stale IWDGRSTF from a previous experiment cannot false-pass.
WDG_MARKER_ADDR = 0x2000F000
WDG_MAGIC = 0x5A5A5A5A
WDG_PHASE_PASSED = 0x5EED

# OpenOCD 12 prints data words without the 0x prefix; the address column
# keeps the prefix (ticket #60 gotcha, tools/probe.py).
_RAM_RE = re.compile(r"0x[0-9a-fA-F]{8}:\s*((?:[0-9a-fA-F]{8}\s+){4}[0-9a-fA-F]{8})")
_MDW_LINE_RE = re.compile(r"0x([0-9a-fA-F]{8}):\s*((?:[0-9a-fA-F]{8}\s*)+)")


def parse_mdw_lines(text):
    """Parse all `mdw` lines into {addr: [words]}. Raises ValueError if none."""
    lines = {}
    for m in _MDW_LINE_RE.finditer(text):
        lines[int(m.group(1), 16)] = [int(w, 16) for w in m.group(2).split()]
    if not lines:
        raise ValueError("no mdw line found in OpenOCD output")
    return lines


def parse_ram_words(text):
    """Extract the 5 words of one OpenOCD `mdw` line. Raises ValueError."""
    m = _RAM_RE.search(text)
    if not m:
        raise ValueError("no mdw line found in OpenOCD output")
    return [int(w, 16) for w in m.group(1).split()]


def interpret_can(words):
    """Map the 5 RAM words to the CanDiagResult fields; verdict in 'pass'."""
    if len(words) != 5:
        return {"pass": False, "error": "expected 5 words, got %d" % len(words)}
    result = dict(zip(CAN_EXPECTED, words))
    result["pass"] = all(result[k] == v for k, v in CAN_EXPECTED.items())
    return result


def interpret_watchdog(csr_word, marker_words):
    """Verdict for the IWDG fire experiment: PASSED marker AND IWDGRSTF set.
    The marker alone or the flag alone is not enough (stale flag / foreign
    RAM content)."""
    marker_ok = (len(marker_words) == 2
                 and marker_words[0] == WDG_MAGIC
                 and marker_words[1] == WDG_PHASE_PASSED)
    fired = bool(csr_word & IWDGRSTF_MASK)
    return {"pass": marker_ok and fired,
            "rcc_csr": "0x%08X" % csr_word,
            "iwdgrstf": fired,
            "marker": marker_words,
            "marker_ok": marker_ok}
