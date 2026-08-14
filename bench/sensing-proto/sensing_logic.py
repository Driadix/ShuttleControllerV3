"""Pure interpretation logic for the sensing prototype (ticket #63,
bench/sensing-proto/). Kept free of hardware/CLI imports so the host tests
run without a bench (same pattern as bench/bringup/bringup_logic.py).

The firmware (firmware/sensing_proto.cpp) writes a pinned diagnostic struct
at 0x20011000 (section .bram_sensing):

  word 0        magic 0x53454E53 ("SENS")
  word 1        version
  word 2        uptime_ms
  word 3        loop_count
  word 4        i2c_reads (successful read transactions)
  word 5        i2c_fails
  word 6        last_tof_slot_ms
  word 7        last_as5600_ms
  word 8        tof_read_us_max
  word 9        tof_read_us_total
  word 10       tof_read_count
  word 11       reserved
  words 12..19  sensor 0 (ToF ID1 0x09, channel reverse)
  words 20..27  sensor 1 (ToF ID2 0x0A, channel forward)
  words 28..35  sensor 2 (ToF ID3 0x0B, pallet reverse)
  words 36..43  sensor 3 (ToF ID4 0x0C, pallet forward)
  words 44..51  sensor 4 (AS5600 0x36, encoder)

Sensor (8 words): raw, raw2, age_ms, state, samples_ok, samples_fail,
last_status, last_sample_ms. State: 0 Starting, 1 Healthy, 2 Degraded,
3 Faulted, 4 Recovering. Status: 0 ok, 1 noack, 2 short, 3 unknown.

Expected bench state (docs/l4-sensor-bench-v3.md, 2026-08-13): 0x09 and 0x0C
present (Healthy), 0x0A/0x0B physically absent (NACK -> Faulted after 3
consecutive failures), AS5600 present (Healthy).
"""

import re

# Diagnostic struct (firmware/sensing_proto.cpp).
DIAG_ADDR = 0x20011000
DIAG_WORDS = 52
MAGIC = 0x53454E53
VERSION = 1

HEADER_WORDS = 12
SENSOR_WORDS = 8

# Cadences (issue #48 section 5, V1 keep): ToF round-robin 4 x 8 ms slot per
# sensor; AS5600 250 ms. Freshness budgets (issue #48 section 2): ToF 300 ms
# class, AS5600 1 s class.
TOF_PERIOD_MS = 32.0   # one sample per sensor per RR cycle (4 x 8 ms)
AS5600_PERIOD_MS = 250.0
TOF_FRESH_MS = 300
AS5600_FRESH_MS = 1000
TOF_SLOT_MS = 8        # bounded slot budget for one read (issue #48 section 7)

# Cadence tolerance: observed samples must be at least this fraction of the
# ideal rate (jitter, occasional slot collision with the 1 ms tick).
CADENCE_FACTOR = 0.6

SENSORS = [
    {"name": "tof_channel_reverse", "addr": 0x09, "kind": "tof", "expect": "present"},
    {"name": "tof_channel_forward", "addr": 0x0A, "kind": "tof", "expect": "absent"},
    {"name": "tof_pallet_reverse", "addr": 0x0B, "kind": "tof", "expect": "absent"},
    {"name": "tof_pallet_forward", "addr": 0x0C, "kind": "tof", "expect": "present"},
    {"name": "as5600", "addr": 0x36, "kind": "as5600", "expect": "present"},
]

STATE_NAMES = {0: "Starting", 1: "Healthy", 2: "Degraded", 3: "Faulted", 4: "Recovering"}
STATUS_NAMES = {0: "ok", 1: "noack", 2: "short", 3: "unknown"}

_MDW_LINE_RE = re.compile(r"0x([0-9a-fA-F]{8}):\s*((?:[0-9a-fA-F]{8}\s*)+)")


def parse_mdw_lines(text):
    """Parse `mdw` output into {addr: [words]}. Raises ValueError if none."""
    lines = {}
    for m in _MDW_LINE_RE.finditer(text):
        lines[int(m.group(1), 16)] = [int(w, 16) for w in m.group(2).split()]
    if not lines:
        raise ValueError("no mdw line found in OpenOCD output")
    return lines


def words_from_mdw(text):
    """Flatten all mdw words in address order (the struct is contiguous from
    DIAG_ADDR). Raises ValueError when fewer than DIAG_WORDS words are found."""
    parsed = parse_mdw_lines(text)
    words = []
    for addr in sorted(parsed):
        words.extend(parsed[addr])
    if len(words) < DIAG_WORDS:
        raise ValueError("expected %d words, got %d" % (DIAG_WORDS, len(words)))
    return words[:DIAG_WORDS]


def _sensor(words, idx):
    base = HEADER_WORDS + idx * SENSOR_WORDS
    return {
        "raw": words[base],
        "raw2": words[base + 1],
        "age_ms": words[base + 2],
        "state": words[base + 3],
        "state_name": STATE_NAMES.get(words[base + 3], "?"),
        "samples_ok": words[base + 4],
        "samples_fail": words[base + 5],
        "last_status": words[base + 6],
        "last_status_name": STATUS_NAMES.get(words[base + 6], "?"),
        "last_sample_ms": words[base + 7],
    }


def interpret_snapshot(words):
    """Map the DIAG_WORDS raw words to a dict; validates magic/version."""
    if len(words) < DIAG_WORDS:
        return {"valid": False, "error": "expected %d words, got %d" % (DIAG_WORDS, len(words))}
    if words[0] != MAGIC:
        return {"valid": False, "error": "magic mismatch 0x%08X" % words[0]}
    if words[1] != VERSION:
        return {"valid": False, "error": "version mismatch %d" % words[1]}
    snap = {
        "valid": True,
        "magic": words[0],
        "version": words[1],
        "uptime_ms": words[2],
        "loop_count": words[3],
        "i2c_reads": words[4],
        "i2c_fails": words[5],
        "last_tof_slot_ms": words[6],
        "last_as5600_ms": words[7],
        "tof_read_us_max": words[8],
        "tof_read_us_total": words[9],
        "tof_read_count": words[10],
        "wire_variant": words[11],
        "sensors": [_sensor(words, i) for i in range(len(SENSORS))],
    }
    for s, meta in zip(snap["sensors"], SENSORS):
        s["name"] = meta["name"]
        s["addr"] = meta["addr"]
        s["kind"] = meta["kind"]
        s["expect"] = meta["expect"]
    return snap


def expected_period_ms(kind):
    return TOF_PERIOD_MS if kind == "tof" else AS5600_PERIOD_MS


def expected_fresh_ms(kind):
    return TOF_FRESH_MS if kind == "tof" else AS5600_FRESH_MS


def expected_state(sensor):
    """Expected health state on the bench record (2026-08-13): present
    sensors Healthy, absent sensors Faulted (NACK threshold reached)."""
    return "Healthy" if sensor["expect"] == "present" else "Faulted"


def cadence_verdict(snap_a, snap_b, window_s):
    """Per-sensor sample rate between two snapshots. PASS when the observed
    delta (samples_ok) is at least CADENCE_FACTOR x the ideal rate for the
    window. Absent sensors are n/a (zero successful samples is expected)."""
    checks = []
    for s_a, s_b in zip(snap_a["sensors"], snap_b["sensors"]):
        if s_b["expect"] != "present":
            checks.append({"name": s_b["name"], "addr": s_b["addr"],
                           "delta": max(0, s_b["samples_ok"] - s_a["samples_ok"]),
                           "ideal": 0.0, "pass": True, "note": "absent, cadence n/a"})
            continue
        delta = max(0, s_b["samples_ok"] - s_a["samples_ok"])
        ideal = window_s * 1000.0 / expected_period_ms(s_b["kind"])
        ok = delta >= CADENCE_FACTOR * ideal
        checks.append({
            "name": s_b["name"],
            "addr": s_b["addr"],
            "delta": delta,
            "ideal": round(ideal, 1),
            "pass": ok,
        })
    return {"pass": all(c["pass"] for c in checks), "checks": checks}


def state_verdict(snap):
    """Health-state expectations on the bench: present -> Healthy (or
    Recovering with >= 3 successes on the way), absent -> Faulted. A sensor
    that is still Starting after the observation window fails (never got a
    sample)."""
    checks = []
    for s in snap["sensors"]:
        expect = expected_state(s)
        observed = s["state_name"]
        ok = observed == expect
        if s["expect"] == "present" and observed == "Recovering":
            ok = s["samples_ok"] >= 3
        checks.append({
            "name": s["name"],
            "addr": s["addr"],
            "expected": expect,
            "observed": observed,
            "samples_ok": s["samples_ok"],
            "pass": ok,
        })
    return {"pass": all(c["pass"] for c in checks), "checks": checks}


def freshness_verdict(snap):
    """Freshness at the snapshot moment: present sensors must be fresh
    (age < budget)."""
    checks = []
    for s in snap["sensors"]:
        if s["expect"] != "present":
            checks.append({"name": s["name"], "addr": s["addr"],
                           "pass": True, "note": "absent, freshness n/a"})
            continue
        budget = expected_fresh_ms(s["kind"])
        ok = s["samples_ok"] > 0 and s["age_ms"] < budget
        checks.append({
            "name": s["name"],
            "addr": s["addr"],
            "age_ms": s["age_ms"],
            "budget_ms": budget,
            "pass": ok,
        })
    return {"pass": all(c["pass"] for c in checks), "checks": checks}


def budget_verdict(snap):
    """One-ToF-read duration (prototype probe for the 8 ms slot / 10 ms step
    budgets, issue #48 sections 4/7). Warning-only in the prototype: the
    production measurement is the L4 scenario of the slice."""
    if snap["tof_read_count"] == 0:
        return {"pass": True, "note": "no tof reads yet", "checks": []}
    ok = snap["tof_read_us_max"] < TOF_SLOT_MS * 1000
    return {
        "pass": ok,
        "note": "prototype probe only",
        "checks": [{
            "name": "tof_read_us_max",
            "us": snap["tof_read_us_max"],
            "budget_us": TOF_SLOT_MS * 1000,
            "pass": ok,
        }],
    }


def full_verdict(snap_a, snap_b, window_s):
    """All checks together. Returns {pass, verdict, checks}."""
    parts = {
        "cadence": cadence_verdict(snap_a, snap_b, window_s),
        "states": state_verdict(snap_b),
        "freshness": freshness_verdict(snap_b),
        "budget": budget_verdict(snap_b),
    }
    ok = all(parts[k]["pass"] for k in parts)
    return {
        "pass": ok,
        "verdict": "PASS" if ok else "FAIL",
        "window_s": window_s,
        "snap_a_uptime_ms": snap_a["uptime_ms"],
        "snap_b_uptime_ms": snap_b["uptime_ms"],
        "checks": parts,
    }
