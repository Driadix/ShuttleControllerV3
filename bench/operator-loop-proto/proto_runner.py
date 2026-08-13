#!/usr/bin/env python3
"""PROTOTYPE - L4 operator loop runner (ticket #60).

Throwaway prototype answering the design question of issue #60: what should a
reproducible L4 operator loop look like?  Detection of board and port, ST-Link
flash, scenario run with timeout, raw UART capture, normalization into a
machine-readable result, binding of firmware SHA + board identity, and refusal
on incomplete evidence.

This is BENCH TOOLING ONLY and explicitly NOT production code.  It exists to
validate flow and formats so issue #65 can implement the production runner
from an approved contract.  Do not reuse it as-is in production paths.

Simulation hooks (prototype mechanics demo WITHOUT hardware):
    --fixture FILE        substitute a raw capture file for the serial port
    --simulate-board      substitute a synthetic board identity record
These exist only so the pipeline mechanics can be demonstrated off-bench;
they are explicitly EXCLUDED from the production runner contract (issue #65):
production evidence must come from a live probe and live capture.

Usage:
    python proto_runner.py detect [--port COM9] [--no-uart]
    python proto_runner.py checklist --owner NAME [--out checklist.json] [--yes]
    python proto_runner.py run <scenario.json> [options]
    python proto_runner.py normalize <raw.bin>

Options for run:
    --port PORT          UART port (default: scenario value or COM9)
    --out-dir DIR        evidence output directory (default: out/<run-id>)
    --checklist FILE     signed checklist JSON (required when scenario.flash)
    --owner NAME         checklist owner (interactive sign-off if no file)
    --no-flash           force-skip the flash step (for dry demos)
    --fixture FILE       simulated capture bytes (no serial port touched)
    --simulate-board     synthetic board identity (no ST-Link probe)

Exit codes:
    0 PASS
    1 FAIL        (oracle violated, evidence complete)
    2 TIMEOUT     (oracle not satisfied within capture window)
    3 INCOMPLETE  (refusal: evidence cannot be made complete)

Requires: pyserial; platformio for the flash step (PIO_CMD env override,
default ".venv-pio312/Scripts/python.exe -m platformio").
"""

from __future__ import annotations

import argparse
import datetime as _dt
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:  # pragma: no cover - environment
    sys.stderr.write("pyserial required: pip install pyserial\n")
    sys.exit(3)

# ---------------------------------------------------------------------------
# Bench facts (docs/l4-sensor-bench-v3.md)
# ---------------------------------------------------------------------------
HDR_LEN = 6  # sync1 sync2 msgID targetID seq length
MAX_PAYLOAD = 120
MAX_FRAME = HDR_LEN + MAX_PAYLOAD + 2
SYNC = bytes((0xBB, 0xCC))
MSG_LOG = 0x10
MSG_SENSORS = 0x02
MSG_AS5600_HEALTH = 0x09

MSG_NAMES = {
    0x02: "MSG_SENSORS",
    0x09: "MSG_AS5600_HEALTH",
    0x10: "MSG_LOG",
}

DEFAULT_PORT = "COM9"
DEFAULT_BAUD = 230400
DEFAULT_PARITY = "E"  # 8E1 (network_bridge display profile)

OPENOCD = os.path.join(
    os.environ.get("USERPROFILE", ""),
    ".platformio", "packages", "tool-openocd", "bin", "openocd.exe",
)
PIO = os.environ.get(
    "PIO_CMD",
    str(Path(".venv-pio312/Scripts/python.exe")) + " -m platformio",
)

SIM_BOARD = {
    "probe": "PASS", "part": "STM32F405RG (simulated)",
    "idcode": "0x100f6413", "uid": None,
}


def utcnow() -> str:
    return _dt.datetime.now(_dt.timezone.utc).isoformat(timespec="seconds")


# ---------------------------------------------------------------------------
# Frame decode (mirrors bench/bridge-relay/tools/capture.py - throwaway dup)
# ---------------------------------------------------------------------------
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_frames(raw: bytes):
    """Yield (msg_id, payload, crc_ok) frames. Lenient: resync on garbage."""
    i = 0
    n = len(raw)
    while i + HDR_LEN + 2 <= n:
        if raw[i] != SYNC[0] or raw[i + 1] != SYNC[1]:
            i += 1
            continue
        msg_id, _target, _seq, length = raw[i + 2], raw[i + 3], raw[i + 4], raw[i + 5]
        if length > MAX_PAYLOAD:
            i += 2
            continue
        end = i + HDR_LEN + length + 2
        if end > n:
            break
        payload = raw[i + HDR_LEN:i + HDR_LEN + length]
        crc_stored = raw[end - 2] | (raw[end - 1] << 8)
        crc_ok = crc16(raw[i:i + HDR_LEN + length]) == crc_stored
        yield msg_id, payload, crc_ok
        i = end


def decode_sensors(payload: bytes) -> str:
    # 16 B: 4 roles x {dist u16 LE, valid u8} - enough for prototype counts
    roles = ("ChR", "ChF", "PlR", "PlF")
    parts = []
    for k in range(4):
        base = k * 4
        dist = payload[base] | (payload[base + 1] << 8) if base + 2 <= len(payload) else 0
        valid = payload[base + 2] if base + 3 <= len(payload) else 0
        parts.append(f"{roles[k]}={'ok' if valid else 'inv'}({dist})")
    return " ".join(parts)


def decode_log(payload: bytes) -> str:
    try:
        return payload.decode("utf-8", errors="replace")
    except Exception:
        return repr(payload)


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
# Board / port detection
# ---------------------------------------------------------------------------
def kill_openocd():
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/F", "/IM", "openocd.exe"],
            capture_output=True, check=False,
        )


def stlink_probe(timeout_s: int = 40):
    """Probe ST-Link via OpenOCD: returns part, idcode, uid or raises."""
    if not os.path.exists(OPENOCD):
        raise RuntimeError(f"openocd not found: {OPENOCD}")
    kill_openocd()
    cfg = [
        OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
        "-c", "init; halt; mdw 0xE0042000 1", "-c", "mdw 0x1FFF7A10 3",
        "-c", "shutdown",
    ]
    r = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    out = (r.stdout + r.stderr)
    # DBGMCU_IDCODE: "0xe0042000: 100f6413" (data words print WITHOUT 0x prefix)
    m_id = re.search(r"0xe0042000:\s*([0-9a-fA-F]{8})", out)
    # Legacy text form: "STM32F405RG (id 0x100f6413)"
    m_part = re.search(r"(\w+)\s+\(id\s+0x[0-9a-fA-F]+\)", out)
    # UID (96-bit): "0x1fff7a10: 00290036 30334703 36363131"
    m_uid = re.search(r"0x1fff7a10:\s*([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})", out)
    if not m_id:
        raise RuntimeError(f"ST-Link probe failed (no DBGMCU idcode): {out[-400:]}")
    dev_id = int(m_id.group(1), 16) & 0xFFF
    part = {0x413: "STM32F405RG", 0x419: "STM32F427/437", 0x411: "STM32F40x"}.get(
        dev_id, f"STM32F4 dev_id=0x{dev_id:03X}")
    if m_part:
        part = m_part.group(1)
    uid = None
    if m_uid:
        uid = "".join(m_uid.groups()).lower()
    return {
        "probe": "PASS",
        "part": part,
        "idcode": "0x" + m_id.group(1),
        "uid": uid,
    }


def uart_check(port: str, baud: int = DEFAULT_BAUD, parity: str = DEFAULT_PARITY,
               timeout_s: float = 2.0):
    """Open the port and read a little; returns open status + bytes seen."""
    par = {"N": serial.PARITY_NONE, "E": serial.PARITY_EVEN,
           "O": serial.PARITY_ODD}.get(parity.upper(), serial.PARITY_EVEN)
    try:
        with serial.Serial(port, baud, parity=par, stopbits=1, timeout=timeout_s) as s:
            time.sleep(0.3)
            n = s.in_waiting
            data = s.read(min(n, 4096)) if n else b""
        return {"port": port, "open": True, "bytes": len(data)}
    except Exception as exc:  # noqa: BLE001 - prototype
        return {"port": port, "open": False, "error": str(exc)}


# ---------------------------------------------------------------------------
# Flash
# ---------------------------------------------------------------------------
def git_identity():
    sha = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                         text=True).stdout.strip()
    desc = subprocess.run(["git", "describe", "--always", "--dirty"],
                          capture_output=True, text=True).stdout.strip()
    return {"gitSha": sha or None, "gitDescribe": desc or None}


def artifact_sha256():
    for name in ("firmware.bin", "firmware.hex", "firmware.elf"):
        p = Path(".pio/build/firmware") / name
        if p.exists():
            return {"artifact": name, "artifactSha256": hashlib.sha256(
                p.read_bytes()).hexdigest()}
    return {"artifact": None, "artifactSha256": None}


def toolchain_identity():
    r = subprocess.run(PIO.split() + ["--version"], capture_output=True, text=True)
    ver = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "unknown"
    return {"platformio": ver}


def flash_firmware(env: str = "firmware", timeout_s: int = 600):
    """Build + upload via ST-Link. Returns step record or raises."""
    t0 = time.time()
    r = subprocess.run(PIO.split() + ["run", "-e", env],
                       capture_output=True, text=True, timeout=timeout_s)
    if r.returncode != 0:
        raise RuntimeError(f"build failed: {r.stdout[-500:]}{r.stderr[-500:]}")
    r2 = subprocess.run(PIO.split() + ["run", "-e", env, "-t", "upload"],
                        capture_output=True, text=True, timeout=timeout_s)
    if r2.returncode != 0:
        raise RuntimeError(f"upload failed: {r2.stdout[-500:]}{r2.stderr[-500:]}")
    return {"ok": True, "env": env, "durationS": round(time.time() - t0, 1)}


# ---------------------------------------------------------------------------
# Checklist (owner split)
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


def make_checklist(owner: str, items=None, assume_yes: bool = False,
                   out: str = "checklist.json"):
    items = items or DEFAULT_ITEMS
    answers = []
    for it in items:
        if assume_yes:
            answers.append(True)
            print(f"  [y] {it}")
        else:
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


# ---------------------------------------------------------------------------
# Capture
# ---------------------------------------------------------------------------
def capture(port: str, duration_s: float, baud: int, parity: str,
            max_bytes: int) -> bytes:
    par = {"N": serial.PARITY_NONE, "E": serial.PARITY_EVEN,
           "O": serial.PARITY_ODD}.get(parity.upper(), serial.PARITY_EVEN)
    buf = bytearray()
    with serial.Serial(port, baud, parity=par, stopbits=1,
                       timeout=0.2) as s:
        s.reset_input_buffer()
        deadline = time.time() + duration_s
        while time.time() < deadline:
            chunk = s.read(s.in_waiting or 1)
            if chunk:
                buf.extend(chunk)
                if len(buf) >= max_bytes:
                    break
    return bytes(buf)


def run_capture(scenario: dict, port: str, out_dir: Path):
    cap = scenario["capture"]
    raw = capture(port, float(cap["durationS"]), int(cap["baud"]),
                  cap.get("parity", DEFAULT_PARITY),
                  int(cap.get("maxBytes", 2_000_000)))
    raw_path = out_dir / f"raw-{scenario['id']}.bin"
    raw_path.write_bytes(raw)
    return raw, str(raw_path)


# ---------------------------------------------------------------------------
# Oracle + verdict
# ---------------------------------------------------------------------------
def evaluate_oracle(scenario: dict, norm: dict) -> tuple:
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


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------
def load_scenario(path: str) -> dict:
    sc = json.loads(Path(path).read_text(encoding="utf-8"))
    if sc.get("schemaVersion") != 1:
        raise RuntimeError(f"unsupported schemaVersion: {sc.get('schemaVersion')}")
    return sc


def run_loop(scenario: dict, port: str, out_dir: Path, checklist_path=None,
             owner: str = None, no_flash: bool = False, fixture: str = None,
             simulate_board: bool = False) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    result = {
        "schemaVersion": 1,
        "runner": "operator-loop-proto (PROTOTYPE, ticket #60)",
        "scenario": {"id": scenario["id"], "schemaVersion": scenario["schemaVersion"]},
        "startedAt": utcnow(),
        "board": None,
        "uart": None,
        "firmware": None,
        "toolchain": None,
        "checklist": None,
        "flash": None,
        "capture": None,
        "verdict": None,
        "reasons": [],
        "evidence": {"complete": False, "missing": []},
    }

    # 0. Checklist gate - FIRST (hard gate: no physical interaction, probe
    #    included, before gates pass; contract §3.1)
    need_flash = bool(scenario.get("flash", {}).get("required")) and not no_flash
    if need_flash:
        if checklist_path and Path(checklist_path).exists():
            cl = json.loads(Path(checklist_path).read_text(encoding="utf-8"))
            result["checklist"] = cl
            if not cl.get("signed"):
                result["evidence"]["missing"].append("checklistSignoff")
        else:
            if owner:
                cl = make_checklist(owner, out=str(out_dir / "checklist.json"),
                                    assume_yes=False)
                result["checklist"] = cl
                if not cl.get("signed"):
                    result["evidence"]["missing"].append("checklistSignoff")
            else:
                result["evidence"]["missing"].append("checklist")

    # 1. Board identity (mandatory evidence) - gated: no physical interaction
    #    (probe included) before gates pass (contract §3.1)
    if simulate_board:
        result["board"] = dict(SIM_BOARD)
    elif result["evidence"]["missing"]:
        result["board"] = {"probe": "SKIPPED",
                           "reason": "pre-flight gate failed: "
                                     + ", ".join(result["evidence"]["missing"])}
        result["evidence"]["missing"].append("boardIdentity")
    else:
        try:
            result["board"] = stlink_probe()
        except Exception as exc:  # noqa: BLE001
            result["board"] = {"probe": "FAIL", "error": str(exc)}
            result["reasons"].append("board probe FAIL")
            result["evidence"]["missing"].append("boardIdentity")

    # 2. UART port check (mandatory evidence)
    if fixture:
        fb = Path(fixture).read_bytes()
        result["uart"] = {"port": f"fixture:{fixture}", "open": True,
                          "bytes": len(fb), "simulated": True}
    else:
        uart = uart_check(port, int(scenario["capture"]["baud"]),
                          scenario["capture"].get("parity", DEFAULT_PARITY))
        result["uart"] = uart
        if not uart["open"]:
            result["evidence"]["missing"].append("uartPort")

    # 4. Flash - ONLY when the pre-flight evidence gates pass (hard gate:
    #    refusal must prevent physical side effects, not just the verdict).
    #    Missing board identity, uart port, or checklist sign-off => skip flash.
    if need_flash:
        if result["evidence"]["missing"]:
            result["flash"] = {
                "ok": False, "skipped": True,
                "reason": "pre-flight evidence incomplete: "
                          + ", ".join(result["evidence"]["missing"]),
            }
        else:
            try:
                result["flash"] = flash_firmware(
                    scenario["flash"].get("env", "firmware"))
            except Exception as exc:  # noqa: BLE001
                result["flash"] = {"ok": False, "error": str(exc)[-300:]}
                result["evidence"]["missing"].append("flash")

    # 5. Firmware + toolchain identity (mandatory evidence)
    result["firmware"] = git_identity()
    result["firmware"].update(artifact_sha256())
    result["toolchain"] = toolchain_identity()
    if not result["firmware"].get("gitSha"):
        result["evidence"]["missing"].append("gitSha")
    if not result["firmware"].get("artifactSha256"):
        result["evidence"]["missing"].append("artifactSha256")

    # 6. Capture + normalize
    if result["uart"] and result["uart"]["open"]:
        if fixture:
            raw = fb
            raw_path = out_dir / f"raw-{scenario['id']}.bin"
            raw_path.write_bytes(raw)
            result["capture"] = {"rawPath": str(raw_path), "rawBytes": len(raw),
                                 "durationS": scenario["capture"]["durationS"]}
        else:
            raw, raw_path = run_capture(scenario, port, out_dir)
            result["capture"] = {"rawPath": raw_path, "rawBytes": len(raw),
                                 "durationS": scenario["capture"]["durationS"]}
        norm = normalize_raw(raw)
        result["normalized"] = norm
    else:
        result["capture"] = {"rawPath": None, "rawBytes": 0,
                             "durationS": scenario["capture"]["durationS"]}
        norm = {"bytes": 0, "framesValid": 0, "framesBad": 0,
                "msgCounts": {}, "logLines": []}
        result["normalized"] = norm

    # 7. Evidence completeness + verdict
    missing = result["evidence"]["missing"]
    result["evidence"]["complete"] = not missing
    if not result["evidence"]["complete"]:
        result["verdict"] = "INCOMPLETE"
        result["reasons"].extend(f"missing evidence: {m}" for m in missing)
    else:
        verdict, reasons = evaluate_oracle(scenario, norm)
        result["verdict"] = verdict
        result["reasons"] = reasons

    result["finishedAt"] = utcnow()
    result_path = out_dir / f"result-{scenario['id']}.json"
    result_path.write_text(json.dumps(result, ensure_ascii=False, indent=2),
                           encoding="utf-8")
    result["evidence"]["resultPath"] = str(result_path)
    return result


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_detect = sub.add_parser("detect", help="ST-Link probe + UART check")
    p_detect.add_argument("--port", default=DEFAULT_PORT)
    p_detect.add_argument("--no-uart", action="store_true")

    p_cl = sub.add_parser("checklist", help="owner safety-checklist sign-off")
    p_cl.add_argument("--owner", required=True)
    p_cl.add_argument("--out", default="checklist.json")
    p_cl.add_argument("--yes", action="store_true")

    p_run = sub.add_parser("run", help="full operator loop for a scenario")
    p_run.add_argument("scenario")
    p_run.add_argument("--port", default=None)
    p_run.add_argument("--out-dir", default=None)
    p_run.add_argument("--checklist", default=None)
    p_run.add_argument("--owner", default=None)
    p_run.add_argument("--no-flash", action="store_true")
    p_run.add_argument("--fixture", default=None)
    p_run.add_argument("--simulate-board", action="store_true")

    p_norm = sub.add_parser("normalize", help="decode a raw capture file")
    p_norm.add_argument("raw")

    args = ap.parse_args(argv)

    if args.cmd == "detect":
        try:
            board = stlink_probe()
            print(json.dumps(board, indent=2))
        except Exception as exc:  # noqa: BLE001
            print(json.dumps({"probe": "FAIL", "error": str(exc)}, indent=2))
            return 3
        if not args.no_uart:
            u = uart_check(args.port)
            print(json.dumps(u, indent=2))
            if not u["open"]:
                return 3
        return 0

    if args.cmd == "checklist":
        rec = make_checklist(args.owner, assume_yes=args.yes, out=args.out)
        print(json.dumps(rec, ensure_ascii=False, indent=2))
        return 0 if rec["signed"] else 3

    if args.cmd == "normalize":
        raw = Path(args.raw).read_bytes()
        print(json.dumps(normalize_raw(raw), ensure_ascii=False, indent=2))
        return 0

    if args.cmd == "run":
        scenario = load_scenario(args.scenario)
        port = args.port or scenario["capture"].get("port") or DEFAULT_PORT
        if port == "auto":
            port = DEFAULT_PORT
        out_dir = Path(args.out_dir) if args.out_dir else (
            Path("out") / f"{scenario['id']}-{time.strftime('%Y%m%d-%H%M%S')}")
        result = run_loop(scenario, port, out_dir,
                          checklist_path=args.checklist, owner=args.owner,
                          no_flash=args.no_flash, fixture=args.fixture,
                          simulate_board=args.simulate_board)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        exit_code = {"PASS": 0, "FAIL": 1, "TIMEOUT": 2, "INCOMPLETE": 3}[
            result["verdict"]]
        print(f"\nVERDICT: {result['verdict']} (exit {exit_code})")
        print(f"evidence: {result['evidence']['resultPath']}")
        return exit_code

    return 2


if __name__ == "__main__":
    sys.exit(main())
