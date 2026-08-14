#!/usr/bin/env python3
"""verification-runner - production L4/L5 verification runner (ticket #65).

Implements the approved operator-loop contract (issue #60,
docs/operator-loop-design-v3.md) as one vertical PR: CLI, versioned scenario
schema, board/port discovery, gated ST-Link flash, bounded UART capture,
normalization, identity binding (observed + expected) and refusal on
incomplete evidence.

Bench tooling: it executes scenarios around the physical bench and produces
evidence. It contains no product behavior and no paths that turn failed
product behavior into a pass (no simulation hooks, no auto-attestation).

Usage:
    runner.py detect [--port COM9] [--no-uart]
    runner.py checklist --owner NAME [--out FILE]
    runner.py run <scenario.json> [--port P] [--checklist F] [--out-dir D] [--owner NAME]
    runner.py normalize <raw.bin>
    runner.py evidence <result.json>

Exit codes:
    0 PASS          (evidence complete, oracle satisfied)
    1 FAIL          (oracle violated, evidence complete)
    2 TIMEOUT       (oracle not satisfied within capture window)
    3 INCOMPLETE    (refusal: evidence cannot be made complete)
    4 SCHEMA ERROR  (scenario invalid, run not started)

Requires: pyserial==3.5 (requirements.txt); PlatformIO for the flash step
(PIO_CMD env override, default venv ".venv-pio312/Scripts/python.exe -m platformio");
OpenOCD from the frozen PlatformIO packages (tool-openocd@3.1200.0).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

try:
    from tools import capture, flash, identity, probe, readback
    from tools.identity import utcnow
except ImportError as exc:  # pragma: no cover - environment
    sys.stderr.write(f"missing dependency: {exc}\n")
    sys.stderr.write("install requirements: "
                     ".venv-pio312/Scripts/python.exe -m pip install "
                     "-r bench/verification-runner/requirements.txt\n")
    sys.exit(3)

# ---------------------------------------------------------------------------
# Bench facts (docs/l4-sensor-bench-v3.md, ticket #73)
# ---------------------------------------------------------------------------
HDR_LEN = 6  # sync1 sync2 msgID targetID seq length
MAX_PAYLOAD = 120
MAX_FRAME = HDR_LEN + MAX_PAYLOAD + 2
SYNC = bytes((0xBB, 0xCC))
MSG_LOG = 0x10

MSG_NAMES = {
    0x02: "MSG_SENSORS",
    0x09: "MSG_AS5600_HEALTH",
    0x10: "MSG_LOG",
}

DEFAULT_PORT = "COM9"
DEFAULT_BAUD = 230400
DEFAULT_PARITY = "E"  # 8E1 (network_bridge display profile)

VERDICT_EXIT = {"PASS": 0, "FAIL": 1, "TIMEOUT": 2, "INCOMPLETE": 3}

# ---------------------------------------------------------------------------
# Schema (scenario v1). schemas/scenario-v1.json is the normative contract
# document; validate_scenario is the executable validator (owner decision,
# design doc section 0.1).
# ---------------------------------------------------------------------------
class SchemaError(RuntimeError):
    """Scenario schema violation - run must not start (contract T11/T12)."""


def validate_scenario(sc):
    """Validate a scenario dict against the v1/v2 contract. Raises SchemaError.

    Every scenario must declare the expected bench board identity
    (identity.board.part + 96-bit uid): identity mismatch is a bench
    misconfiguration and must be an explicit non-pass (ticket #65 Acceptance).

    v2 is additive: `readback` declares a RAM observation (pinned diagnostic
    struct, ticket #63) with explicit rules; `capture` becomes optional. The
    runner stays generic - rule semantics live in the scenario.
    """
    if not isinstance(sc, dict):
        raise SchemaError("scenario must be a JSON object")
    version = sc.get("schemaVersion")
    if version not in (1, 2):
        raise SchemaError(f"unsupported schemaVersion: {version!r}")
    for key in ("id", "title", "type", "phase"):
        if not isinstance(sc.get(key), str) or not sc[key]:
            raise SchemaError(f"scenario.{key} must be a non-empty string")
    if not re.fullmatch(r"[a-z0-9-]+", sc["id"]):
        raise SchemaError("scenario.id must be a slug matching [a-z0-9-]+ "
                          "(used in artifact filenames)")
    stype = sc["type"]
    if stype not in ("behavior", "flash-verify"):
        raise SchemaError(f"type must be 'behavior'|'flash-verify', got: {stype!r}")
    if sc["phase"] not in ("L4", "L5"):
        raise SchemaError(f"phase must be 'L4'|'L5', got: {sc['phase']!r}")

    ident = sc.get("identity")
    if not isinstance(ident, dict):
        raise SchemaError("scenario.identity must be an object")
    bident = ident.get("board")
    if not isinstance(bident, dict) or not isinstance(bident.get("part"), str) \
            or not bident["part"]:
        raise SchemaError("scenario.identity.board.part must be a non-empty string")
    uid = bident.get("uid")
    if not isinstance(uid, str) or not re.fullmatch(r"[0-9a-fA-F]{24}", uid):
        raise SchemaError("scenario.identity.board.uid must be a 24-hex-char "
                          "string (96-bit device UID)")

    if version == 1:
        if not isinstance(sc.get("capture"), dict):
            raise SchemaError("scenario.capture must be an object (v1)")
        for key in ("port", "baud", "parity", "durationS", "maxBytes"):
            if key not in sc["capture"]:
                raise SchemaError(f"scenario.capture.{key} is required (v1)")
    else:
        rb = sc.get("readback")
        if not isinstance(rb, dict):
            raise SchemaError("scenario.readback must be an object (v2)")
        if not isinstance(rb.get("addr"), int) or rb["addr"] <= 0:
            raise SchemaError("scenario.readback.addr must be a positive int")
        if not isinstance(rb.get("words"), int) or rb["words"] <= 0:
            raise SchemaError("scenario.readback.words must be a positive int")
        if not isinstance(rb.get("windowS"), (int, float)) or rb["windowS"] <= 0:
            raise SchemaError("scenario.readback.windowS must be > 0")
        rules = rb.get("rules")
        if not isinstance(rules, list) or not rules:
            raise SchemaError("scenario.readback.rules must be a non-empty list")
        for rule in rules:
            if not isinstance(rule, dict) or not isinstance(rule.get("name"), str):
                raise SchemaError("readback rule must be {name, offset, ...}")
            if not isinstance(rule.get("offset"), int) or rule["offset"] < 0:
                raise SchemaError(f"rule {rule.get('name')!r}: offset must be >= 0")
            kind = rule.get("kind")
            if kind == "delta":
                if not isinstance(rule.get("minDelta"), (int, float)):
                    raise SchemaError(f"rule {rule.get('name')!r}: "
                                      "delta rule needs minDelta")
            elif kind == "eq":
                if "expect" not in rule:
                    raise SchemaError(f"rule {rule.get('name')!r}: eq rule needs expect")
            elif kind == "max":
                if not isinstance(rule.get("max"), (int, float)):
                    raise SchemaError(f"rule {rule.get('name')!r}: max rule needs max")
            else:
                raise SchemaError(f"rule {rule.get('name')!r}: kind must be "
                                  "'delta'|'eq'|'max'")
        cap = sc.get("capture")
        if cap is not None and not isinstance(cap, dict):
            raise SchemaError("scenario.capture must be an object or absent (v2)")

    fl = sc.get("flash")
    if not isinstance(fl, dict) or not isinstance(fl.get("required"), bool):
        raise SchemaError("scenario.flash.required must be a boolean")
    if fl["required"] and (not isinstance(fl.get("env"), str) or not fl["env"]):
        raise SchemaError("scenario.flash.env must be a non-empty string "
                          "when flash.required")

    cap = sc.get("capture")
    if cap is not None:
        port = cap.get("port")
        if not isinstance(port, str) or not port:
            raise SchemaError("scenario.capture.port must be a non-empty string "
                              "('auto' or a COM port)")
        for key, typ in (("baud", int), ("durationS", (int, float)),
                         ("maxBytes", int)):
            val = cap.get(key)
            if not isinstance(val, typ) or isinstance(val, bool) or val <= 0:
                raise SchemaError(f"scenario.capture.{key} must be a positive number")
        parity = cap.get("parity")
        if parity not in ("N", "E", "O"):
            raise SchemaError(f"scenario.capture.parity must be 'N'|'E'|'O', "
                              f"got: {parity!r}")

    o = sc.get("oracle")
    if version == 1 and not isinstance(o, dict):
        raise SchemaError("scenario.oracle must be an object (v1)")
    if o is not None:
        min_frames = o.get("minFrames")
        if not isinstance(min_frames, int) or isinstance(min_frames, bool) \
                or min_frames < 0:
            raise SchemaError("scenario.oracle.minFrames must be a non-negative integer")
        max_bad = o.get("maxCrcBadRatio")
        if not isinstance(max_bad, (int, float)) or isinstance(max_bad, bool) \
                or not (0.0 <= max_bad <= 1.0):
            raise SchemaError("scenario.oracle.maxCrcBadRatio must be in [0.0, 1.0]")
        req = o.get("requirePatterns")
        forb = o.get("forbidPatterns")
        for name, pats in (("requirePatterns", req), ("forbidPatterns", forb)):
            if not isinstance(pats, list) or not all(isinstance(p, str) for p in pats):
                raise SchemaError(f"scenario.oracle.{name} must be a list of strings")
            for p in pats:
                try:
                    re.compile(p)
                except re.error as exc:
                    raise SchemaError(f"scenario.oracle.{name} invalid regex "
                                      f"{p!r}: {exc}")
        if stype == "behavior":
            # Non-vacuous rule (issue #60 section 2.1, T11): behavior must have
            # an observable positive; silence is never PASS of behavior.
            if min_frames < 1 and not req:
                raise SchemaError(
                    "behavior scenario must have minFrames>=1 or non-empty "
                    "requirePatterns (vacuous PASS prohibited)")
        else:  # flash-verify: PASS claims only flash + probe alive, never behavior
            if min_frames != 0 or req or forb:
                raise SchemaError(
                    "flash-verify scenario must have minFrames=0 and empty "
                    "pattern lists (no behavior claim allowed)")
    return sc


# ---------------------------------------------------------------------------
# Frame decode (mirrors bench/bridge-relay/tools/capture.py - bench contract)
# ---------------------------------------------------------------------------
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 \
                else (crc << 1) & 0xFFFF
    return crc


def decode_frames(raw: bytes):
    """Yield (msg_id, payload, crc_ok) frames. Lenient: resync on garbage."""
    i = 0
    n = len(raw)
    while i + HDR_LEN + 2 <= n:
        if raw[i] != SYNC[0] or raw[i + 1] != SYNC[1]:
            i += 1
            continue
        _msg, _target, _seq, length = raw[i + 2], raw[i + 3], raw[i + 4], raw[i + 5]
        if length > MAX_PAYLOAD:
            i += 2
            continue
        end = i + HDR_LEN + length + 2
        if end > n:
            break
        payload = raw[i + HDR_LEN:i + HDR_LEN + length]
        crc_stored = raw[end - 2] | (raw[end - 1] << 8)
        crc_ok = crc16(raw[i:i + HDR_LEN + length]) == crc_stored
        yield raw[i + 2], payload, crc_ok
        i = end


def decode_log(payload: bytes) -> str:
    return payload.decode("utf-8", errors="replace")


def normalize_raw(raw: bytes):
    """Normalize raw bytes into machine-readable frame stats + log lines."""
    frames_valid = 0
    frames_bad = 0
    msg_counts = {}
    log_lines = []
    for msg_id, payload, ok in decode_frames(raw):
        name = MSG_NAMES.get(msg_id, f"MSG_0x{msg_id:02X}")
        if ok:
            frames_valid += 1
        else:
            frames_bad += 1
        msg_counts[name] = msg_counts.get(name, 0) + 1
        if ok and msg_id == MSG_LOG:
            log_lines.append(decode_log(payload))
    return {
        "bytes": len(raw),
        "framesValid": frames_valid,
        "framesBad": frames_bad,
        "msgCounts": msg_counts,
        "logLines": log_lines,
    }


# ---------------------------------------------------------------------------
# Oracle + verdict (issue #60 section 3.3)
# ---------------------------------------------------------------------------
def evaluate_oracle(scenario: dict, norm: dict):
    """Return (verdict, reasons). PASS/TIMEOUT/FAIL - evidence assumed complete."""
    oracle = scenario.get("oracle", {})
    reasons = []
    min_frames = int(oracle.get("minFrames", 0))
    max_bad_ratio = float(oracle.get("maxCrcBadRatio", 1.0))
    total = norm["framesValid"] + norm["framesBad"]
    bad_ratio = norm["framesBad"] / total if total else 0.0

    if total < min_frames:
        return "TIMEOUT", [f"frames {total} < minFrames {min_frames}"]
    if bad_ratio > max_bad_ratio:
        reasons.append(f"crcBadRatio {bad_ratio:.2f} > {max_bad_ratio}")
        return "FAIL", reasons
    for pat in oracle.get("forbidPatterns", []):
        if any(re.search(pat, line) for line in norm["logLines"]):
            reasons.append(f"forbidden pattern matched: {pat}")
            return "FAIL", reasons
    for pat in oracle.get("requirePatterns", []):
        if not any(re.search(pat, line) for line in norm["logLines"]):
            reasons.append(f"required pattern not found: {pat}")
            return "TIMEOUT", reasons
    return "PASS", reasons


def evaluate_readback_oracle(scenario: dict, norm: dict):
    """Readback (v2) verdict from two RAM snapshots. Every rule is checked:
    eq -> word value must match; delta -> B.offset - A.offset >= minDelta;
    max -> B.offset < max (an upper bound: freshness age, step duration).
    A delta rule that did not accumulate within the window is TIMEOUT (the
    firmware was not acquiring); an eq/max violation is FAIL. Returns
    (verdict, reasons)."""
    rb = scenario.get("readback", {})
    rules = rb.get("rules", [])
    snap_a = norm.get("snapA") or []
    snap_b = norm.get("snapB") or []
    reasons = []
    for rule in rules:
        off = int(rule["offset"])
        name = rule["name"]
        if off >= len(snap_a) or off >= len(snap_b):
            reasons.append(f"rule {name}: offset {off} out of range")
            continue
        if rule["kind"] == "eq":
            if snap_b[off] != int(rule["expect"]):
                reasons.append(
                    f"rule {name}: word[{off}] = {snap_b[off]} != "
                    f"{rule['expect']}")
        elif rule["kind"] == "max":
            if snap_b[off] >= int(rule["max"]):
                reasons.append(
                    f"rule {name}: word[{off}] = {snap_b[off]} >= max "
                    f"{rule['max']}")
        else:  # delta
            delta = snap_b[off] - snap_a[off]
            if delta < int(rule["minDelta"]):
                return "TIMEOUT", [
                    f"rule {name}: delta {delta} < minDelta "
                    f"{rule['minDelta']} over {rb.get('windowS')}s"]
    if reasons:
        return "FAIL", reasons
    return "PASS", reasons


# ---------------------------------------------------------------------------
# Checklist (owner split, issue #60 section 4.3)
# ---------------------------------------------------------------------------
DEFAULT_ITEMS = [
    "Плата обесточена перед коммутацией (оба USB отключены)",
    "ST-Link V2 подключен на XT21 (SWDIO/SWCLK/GND/3.3V_1)",
    "UART-путь подключен на XT22 (relay-дисплей или конвертер)",
    "Датчики подключены (WS_Sensor/XT28: 5V/GNDREF/SCL/SDA)",
    "Питание включено; напряжения 3.3V_1 ~= 3.3 V, 5V ~= 5 V (gate)",
    "Нет нагрева/запаха/дыма; правило аварийной остановки принято",
    "Физические перекоммутации только при обесточенной плате",
]


def make_checklist(owner: str, items=None, out: str = "checklist.json"):
    """Interactive owner sign-off. No auto-attestation (contract #60 4.2.4)."""
    items = items or DEFAULT_ITEMS
    answers = []
    for it in items:
        ans = input(f"  [{it}] y/N: ").strip().lower()
        answers.append(ans in ("y", "yes"))
    rec = {
        "schemaVersion": 1,
        "signed": all(answers),
        "owner": owner,
        "at": utcnow(),
        "items": [{"text": t, "confirmed": a} for t, a in zip(items, answers)],
    }
    Path(out).write_text(json.dumps(rec, ensure_ascii=False, indent=2),
                         encoding="utf-8")
    return rec


def validate_checklist(cl) -> str | None:
    """Validate a loaded owner checklist; return the first failure reason.

    A pre-signed file authorizes physical operations, so its shape is checked
    in full: a forged `{"signed": true}` must not pass (no fake paths, issue
    #60 section 4.2.4). Reasons mirror the result schema checklist fields
    (schemas/result-v1.json).
    """
    if not isinstance(cl, dict):
        return "checklistNotObject"
    if cl.get("schemaVersion") != 1:
        return "checklistSchemaVersion"
    if cl.get("signed") is not True:
        return "checklistSignoff"
    if not isinstance(cl.get("owner"), str) or not cl["owner"]:
        return "checklistOwner"
    if not isinstance(cl.get("at"), str) or not cl["at"]:
        return "checklistTimestamp"
    items = cl.get("items")
    if not isinstance(items, list) or not items:
        return "checklistItems"
    for item in items:
        if not isinstance(item, dict) or item.get("confirmed") is not True:
            return "checklistItemUnconfirmed"
        if not isinstance(item.get("text"), str) or not item["text"]:
            return "checklistItemText"
    return None


# ---------------------------------------------------------------------------
# Operator loop (gate order is a contract invariant, issue #60 section 3.1)
# ---------------------------------------------------------------------------
_EMPTY_NORM = {"bytes": 0, "framesValid": 0, "framesBad": 0,
               "msgCounts": {}, "logLines": []}


def run_loop(scenario: dict, port: str, out_dir: Path, checklist_path=None,
             owner: str = None) -> dict:
    """Execute the operator loop for a scenario; returns the result dict.

    Refusal (non-empty missing set) blocks ALL physical side effects - probe,
    flash and capture included (invariant, issue #60 section 4.2.2). The
    expected bench identity (scenario.identity.board) is compared against the
    probe result; mismatch is an explicit non-pass and blocks flash/capture.
    """
    out_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "schemaVersion": 1,
        "runner": "verification-runner",
        "scenario": {"id": scenario["id"], "schemaVersion": scenario["schemaVersion"]},
        "startedAt": utcnow(),
        "board": None,
        "boardExpected": dict(scenario["identity"]["board"]),
        "uart": None,
        "firmware": None,
        "toolchain": None,
        "checklist": None,
        "flash": None,
        "capture": None,
        "normalized": None,
        "verdict": None,
        "reasons": [],
        "evidence": {"complete": False, "missing": []},
    }
    missing = result["evidence"]["missing"]

    # 1. Checklist gate - FIRST and MANDATORY for every run. No physical
    #    interaction with the bench (probe included) happens before owner
    #    sign-off (issue #60 section 0.3: hard gate before ANY physical
    #    interaction; without sign-off - refusal without side effects).
    #    The #60 section 3.1 conditionality "(if scenario.flash.required)"
    #    was a prototype defect carried from the throwaway runner: probe
    #    halts the MCU and touches the bench, so non-flash runs are gated
    #    too. Standalone `detect` stays read-only, not gated (#60 section 4.3).
    need_flash = bool(scenario.get("flash", {}).get("required"))
    if checklist_path and Path(checklist_path).exists():
        try:
            cl = json.loads(Path(checklist_path).read_text(encoding="utf-8"))
        except (json.JSONDecodeError, OSError) as exc:
            result["checklist"] = {"schemaVersion": 1, "signed": False,
                                   "owner": "", "at": utcnow(), "items": [],
                                   "error": f"unreadable checklist: {exc}"}
            missing.append("checklistSignoff")
        else:
            reason = validate_checklist(cl)
            if reason:
                result["checklist"] = {
                    "schemaVersion": 1, "signed": False,
                    "owner": "", "at": utcnow(), "items": [],
                    "error": f"checklist failed validation: {reason}",
                }
                missing.append(reason)
            else:
                result["checklist"] = cl
    elif owner:
        cl = make_checklist(owner, out=str(out_dir / "checklist.json"))
        result["checklist"] = cl
        if not cl.get("signed"):
            missing.append("checklistSignoff")
    else:
        missing.append("checklist")

    # 2. Board identity (mandatory evidence) - gated; expected identity is
    #    compared and mismatch is an explicit non-pass (ticket #65 Acceptance)
    if missing:
        result["board"] = {"probe": "SKIPPED",
                           "reason": "pre-flight gate failed: " + ", ".join(missing)}
        missing.append("boardIdentity")
    else:
        try:
            board = probe.stlink_probe()
            result["board"] = board
            exp_part = scenario["identity"]["board"]["part"]
            exp_uid = scenario["identity"]["board"]["uid"].lower()
            obs_part = board.get("part")
            obs_uid = (board.get("uid") or "").lower()
            if obs_part != exp_part or obs_uid != exp_uid:
                result["reasons"].append(
                    f"board identity mismatch: expected "
                    f"{exp_part}/{exp_uid}, observed {obs_part}/{obs_uid}")
                missing.append("boardIdentityMismatch")
        except Exception as exc:  # noqa: BLE001 - probe failure is a result
            result["board"] = {"probe": "FAIL", "error": str(exc)}
            result["reasons"].append("board probe FAIL")
            missing.append("boardIdentity")

    # 3. UART port check (passive host check, mandatory v1 evidence) - gated
    #    like the probe: opening the physical port is physical interaction,
    #    so a refused run emits a skipped record instead of touching the
    #    bench (issue #60 section 0.3, hard stop before ANY physical ops).
    #    v2 readback scenarios observe RAM (ticket #63, UART is silent in
    #    Phase 1), so the UART check is optional there.
    has_capture = bool(scenario.get("capture"))
    if not has_capture:
        result["uart"] = {"port": None, "open": None,
                          "note": "readback scenario (v2): no UART capture"}
    elif missing:
        result["uart"] = {"port": port, "open": False,
                          "reason": "pre-flight gate failed: "
                                    + ", ".join(missing)}
        missing.append("uartPort")
    else:
        uart = probe.uart_check(port, int(scenario["capture"]["baud"]),
                                scenario["capture"].get("parity",
                                                         DEFAULT_PARITY))
        result["uart"] = uart
        if not uart["open"]:
            missing.append("uartPort")

    # 4. Flash - ONLY when pre-flight gates pass (refusal prevents side effects)
    if need_flash:
        if missing:
            result["flash"] = {
                "ok": False, "skipped": True,
                "reason": "pre-flight evidence incomplete: "
                          + ", ".join(missing),
            }
        else:
            try:
                result["flash"] = flash.flash_firmware(
                    scenario["flash"].get("env", "firmware"))
            except Exception as exc:  # noqa: BLE001 - flash failure is a result
                result["flash"] = {"ok": False, "error": str(exc)[-300:]}
                missing.append("flash")

    # 5. Firmware + toolchain identity (mandatory evidence; the artifact is
    #    created by the flash build, so it is collected only after step 4).
    #    Toolchain records observed {platformio, platform, core} from
    #    `pio --version` + `pio pkg list`; pin comparison against #51 section 3
    #    is a separate CI contract (tools/check_toolchain.py), not the runner.
    result["firmware"] = identity.git_identity()
    result["firmware"].update(identity.artifact_sha256())
    result["toolchain"] = identity.toolchain_identity()
    if not result["firmware"].get("gitSha"):
        missing.append("gitSha")
    if not result["firmware"].get("artifactSha256"):
        missing.append("artifactSha256")

    # 6. Observation: capture (v1, UART) or readback (v2, RAM snapshots).
    if not has_capture:
        # v2: two RAM snapshots over the observation window. read_ram_words
        # halts the MCU (physical interaction) - gated like probe/flash: a
        # refused run records a skipped observation, never touches the bench
        # (issue #60 section 0.3, hard stop before ANY physical ops).
        if missing:
            result["capture"] = {
                "rawPath": None, "rawBytes": 0,
                "durationS": scenario["readback"]["windowS"],
                "skipped": True,
                "reason": "pre-flight evidence incomplete: "
                          + ", ".join(missing),
            }
            result["normalized"] = {"snapA": [], "snapB": [],
                                    "windowS": scenario["readback"]["windowS"]}
        else:
            try:
                rb = scenario["readback"]
                addr = int(rb["addr"])
                words = int(rb["words"])
                window = float(rb["windowS"])
                # Snapshot A immediately (the flash->identity steps already
                # elapsed; the firmware counts from reset run), B after the
                # full window: the delta rules in the scenario accumulate
                # over exactly `window` (review MAJOR fix, #63).
                snap_a = readback.read_ram_words(addr, words)
                time.sleep(window)
                snap_b = readback.read_ram_words(addr, words)
                a_path = out_dir / f"raw-{scenario['id']}-a.mdw"
                b_path = out_dir / f"raw-{scenario['id']}-b.mdw"
                a_path.write_text(
                    "\n".join("0x%08X: %s" % (addr + i * 4, "%08X" % w)
                              for i, w in enumerate(snap_a)) + "\n",
                    encoding="utf-8")
                b_path.write_text(
                    "\n".join("0x%08X: %s" % (addr + i * 4, "%08X" % w)
                              for i, w in enumerate(snap_b)) + "\n",
                    encoding="utf-8")
                result["capture"] = {"rawPath": str(a_path), "rawBytes": 0,
                                     "durationS": window,
                                     "snapshotPathB": str(b_path)}
                result["normalized"] = {"snapA": snap_a, "snapB": snap_b,
                                        "windowS": window}
            except Exception as exc:  # noqa: BLE001 - readback failure is a result
                result["capture"] = {"rawPath": None, "rawBytes": 0,
                                     "durationS": scenario["readback"]["windowS"],
                                     "error": str(exc)[-300:]}
                result["normalized"] = {"snapA": [], "snapB": [],
                                        "windowS": scenario["readback"]["windowS"]}
                missing.append("capture")
    elif missing:
        result["capture"] = {"rawPath": None, "rawBytes": 0,
                             "durationS": scenario["capture"]["durationS"]}
        result["normalized"] = dict(_EMPTY_NORM)
    elif result["uart"] and result["uart"]["open"]:
        try:
            raw, raw_path = capture.run_capture(scenario, port, out_dir)
            result["capture"] = {"rawPath": raw_path, "rawBytes": len(raw),
                                 "durationS": scenario["capture"]["durationS"]}
            result["normalized"] = normalize_raw(raw)
        except Exception as exc:  # noqa: BLE001 - capture failure is a result
            result["capture"] = {"rawPath": None, "rawBytes": 0,
                                 "durationS": scenario["capture"]["durationS"],
                                 "error": str(exc)[-300:]}
            result["normalized"] = dict(_EMPTY_NORM)
            missing.append("capture")
    else:
        result["capture"] = {"rawPath": None, "rawBytes": 0,
                             "durationS": scenario["capture"]["durationS"]}
        result["normalized"] = dict(_EMPTY_NORM)

    # 7. Evidence completeness + verdict
    result["evidence"]["complete"] = not missing
    if not result["evidence"]["complete"]:
        result["verdict"] = "INCOMPLETE"
        result["reasons"].extend(f"missing evidence: {m}" for m in missing)
    elif has_capture:
        verdict, reasons = evaluate_oracle(scenario, result["normalized"])
        result["verdict"] = verdict
        result["reasons"] = reasons
    else:
        verdict, reasons = evaluate_readback_oracle(scenario,
                                                    result["normalized"])
        result["verdict"] = verdict
        result["reasons"] = reasons

    result["finishedAt"] = utcnow()
    result_path = out_dir / f"result-{scenario['id']}.json"
    # Deterministic self-describing path, computed BEFORE write (POSIX style,
    # owner decision, design doc section 0.4).
    result["evidence"]["resultPath"] = result_path.as_posix()
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2),
                           encoding="utf-8")
    return result


# ---------------------------------------------------------------------------
# Evidence bundle re-check (`evidence` subcommand)
# ---------------------------------------------------------------------------
def check_evidence(result: dict, bundle_dir: Path) -> list:
    """Re-check evidence bundle completeness; returns the missing list."""
    missing = []
    if not isinstance(result, dict):
        return ["resultNotObject"]
    if result.get("schemaVersion") not in (1, 2):
        missing.append("schemaVersion")
    if result.get("verdict") not in VERDICT_EXIT:
        missing.append("verdict")
    missing.extend(result.get("evidence", {}).get("missing") or [])
    if not isinstance(result.get("normalized"), dict):
        missing.append("normalized")
    raw_name = f"raw-{result.get('scenario', {}).get('id', '?')}.bin"
    if not (bundle_dir / raw_name).exists():
        # v2 readback scenarios write raw-<id>-a.mdw / -b.mdw snapshots.
        raw_a = f"raw-{result.get('scenario', {}).get('id', '?')}-a.mdw"
        raw_b = f"raw-{result.get('scenario', {}).get('id', '?')}-b.mdw"
        if not ((bundle_dir / raw_a).exists() and (bundle_dir / raw_b).exists()):
            missing.append(f"rawArtifact:{raw_name}")
    if result.get("evidence", {}).get("complete") is not True:
        missing.append("evidenceComplete")
    return missing


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_detect = sub.add_parser("detect", help="ST-Link probe + UART check (read-only)")
    p_detect.add_argument("--port", default=DEFAULT_PORT)
    p_detect.add_argument("--no-uart", action="store_true")

    p_cl = sub.add_parser("checklist", help="owner safety-checklist sign-off")
    p_cl.add_argument("--owner", required=True)
    p_cl.add_argument("--out", default="checklist.json")

    p_run = sub.add_parser("run", help="full operator loop for a scenario")
    p_run.add_argument("scenario")
    p_run.add_argument("--port", default=None)
    p_run.add_argument("--out-dir", default=None)
    p_run.add_argument("--checklist", default=None)
    p_run.add_argument("--owner", default=None)

    p_norm = sub.add_parser("normalize", help="decode a raw capture file")
    p_norm.add_argument("raw")

    p_ev = sub.add_parser("evidence", help="re-check evidence bundle completeness")
    p_ev.add_argument("result")

    args = ap.parse_args(argv)

    if args.cmd == "detect":
        try:
            board = probe.stlink_probe()
        except Exception as exc:  # noqa: BLE001
            print(json.dumps({"probe": "FAIL", "error": str(exc)}, indent=2))
            return 3
        print(json.dumps(board, indent=2))
        if not args.no_uart:
            u = probe.uart_check(args.port)
            print(json.dumps(u, indent=2))
            if not u["open"]:
                return 3
        return 0

    if args.cmd == "checklist":
        rec = make_checklist(args.owner, out=args.out)
        print(json.dumps(rec, ensure_ascii=False, indent=2))
        return 0 if rec["signed"] else 3

    if args.cmd == "normalize":
        raw = Path(args.raw).read_bytes()
        print(json.dumps(normalize_raw(raw), ensure_ascii=False, indent=2))
        return 0

    if args.cmd == "run":
        try:
            raw_sc = json.loads(Path(args.scenario).read_text(encoding="utf-8"))
            scenario = validate_scenario(raw_sc)
        except (SchemaError, json.JSONDecodeError, OSError) as exc:
            print(f"SCHEMA ERROR: {exc}", file=sys.stderr)
            print("run not started (schema error before any side effect)",
                  file=sys.stderr)
            return 4
        port = args.port or scenario.get("capture", {}).get("port") or DEFAULT_PORT
        if port == "auto":
            port = DEFAULT_PORT
        out_dir = Path(args.out_dir) if args.out_dir else (
            Path("out") / f"{scenario['id']}-{time.strftime('%Y%m%d-%H%M%S')}")
        result = run_loop(scenario, port, out_dir,
                          checklist_path=args.checklist, owner=args.owner)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        exit_code = VERDICT_EXIT[result["verdict"]]
        print(f"\nVERDICT: {result['verdict']} (exit {exit_code})")
        print(f"evidence: {result['evidence']['resultPath']}")
        return exit_code

    if args.cmd == "evidence":
        rp = Path(args.result)
        try:
            result = json.loads(rp.read_text(encoding="utf-8"))
        except Exception as exc:  # noqa: BLE001
            print(f"EVIDENCE ERROR: unreadable result: {exc}", file=sys.stderr)
            return 3
        missing = check_evidence(result, rp.parent)
        print(json.dumps({"complete": not missing, "missing": missing},
                         ensure_ascii=False, indent=2))
        return 0 if not missing else 3

    return 2


if __name__ == "__main__":
    sys.exit(main())
