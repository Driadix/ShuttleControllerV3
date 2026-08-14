"""Generate OFF-bench demo fixtures for the sensing prototype verdict
(ticket #63): synthetic OpenOCD mdw captures of the pinned diagnostic struct.
The `verdict` command runs on these without any hardware, demonstrating the
PASS and FAIL (low cadence) verdict paths.

Usage: .venv-pio312/Scripts/python.exe bench/sensing-proto/gen_fixture.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import sensing_logic as sl  # noqa: E402

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")


def build_words(uptime_ms, tof_ok, as5600_ok, tof_fail, age, tof_read_us_max=1500):
    words = [sl.MAGIC, sl.VERSION, uptime_ms, 100, 1000, 40,
             4900, 4900, tof_read_us_max, 80000, 100, 0]
    for i in range(4):
        ok = tof_ok[i]
        fail = tof_fail[i]
        present = i in (0, 3)
        state = 1 if present else 3
        age_ms = age[i] if present else 0xFFFFFFFF
        words += [1000 + i, 42, age_ms, state, ok, fail, 0 if present else 1,
                  uptime_ms - 5]
    words += [1234, 4321, age[4], 1, as5600_ok, 0, 0, uptime_ms - 30]
    return words


def mdw_text(words, per_line=16):
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i:i + per_line]
        addr = sl.DIAG_ADDR + i * 4
        lines.append("0x%08X: %s" % (addr, " ".join("%08X" % w for w in chunk)))
    return "\n".join(lines) + "\n"


def main():
    # PASS: 5 s window, ToF present sensors gained ~150 samples (~31/s ideal,
    # 60% floor ~94), absent sensors keep failing (NACK -> Faulted), AS5600
    # gained 20 (4/s ideal), fresh at snapshot B, one-read 1.5 ms < 8 ms slot.
    a_pass = build_words(5000, (150, 0, 0, 150), 20, (0, 20, 20, 0), (5, 5, 5, 5, 30))
    b_pass = build_words(10000, (300, 0, 0, 300), 40, (0, 40, 40, 0), (6, 5, 5, 7, 30))
    # FAIL: cadence collapses (present ToF gained only 10 samples in 5 s).
    b_fail = build_words(10000, (160, 0, 0, 160), 25, (0, 40, 40, 0), (6, 5, 5, 7, 30))

    files = {
        "snap-a-pass.mdw": a_pass,
        "snap-b-pass.mdw": b_pass,
        "snap-b-fail-cadence.mdw": b_fail,
    }
    for name, words in files.items():
        path = os.path.join(FIXTURES, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(mdw_text(words))
        print("wrote %s" % path)


if __name__ == "__main__":
    main()
