"""Sensing prototype CLI (ticket #63, bench/sensing-proto/). Drives the
throwaway acquisition probe around the physical bench: gated flash, bounded
observation window, two RAM snapshots over OpenOCD, normalized verdict and
evidence bundle. Same split as the verification runner (#60/#65): the owner
signs the physical checklist per run; the agent runs probe/flash/readback.

Exit codes (runner convention, #60): 0 PASS, 1 FAIL, 2 TIMEOUT, 3 INCOMPLETE
(refusal before side effects), 4 tool error.

Commands:
  probe                          read-only: ST-Link + current RAM snapshot
  readback                       read-only: current RAM snapshot (JSON)
  run --duration N [--checklist F] [--out-dir D]   full cycle + verdict
  verdict --from-a F --from-b F --window S          verdict from two mdw files
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "verification-runner"))

from flash_sensing import flash_sensing, readback_words  # noqa: E402
from sensing_logic import (  # noqa: E402
    DIAG_ADDR, DIAG_WORDS, interpret_snapshot, full_verdict, words_from_mdw,
)
from tools.probe import stlink_probe  # noqa: E402


def _checklist_ok(path):
    """Validate the owner's physical checklist (form check, #60 section 0.3):
    schemaVersion 1, signed true, non-empty owner, all items confirmed true.
    Returns (ok, reason)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as exc:  # noqa: BLE001
        return False, "unreadable checklist: %s" % exc
    if not isinstance(data, dict):
        return False, "checklist is not an object"
    if data.get("schemaVersion") != 1:
        return False, "checklist schemaVersion != 1"
    if data.get("signed") is not True:
        return False, "checklist not signed"
    if not data.get("owner"):
        return False, "checklist owner missing"
    items = data.get("items")
    if not isinstance(items, list) or not items:
        return False, "checklist items missing"
    if not all(isinstance(i, dict) and i.get("confirmed") is True for i in items):
        return False, "checklist has unconfirmed items"
    return True, "ok"


def cmd_probe():
    board = stlink_probe()
    print(json.dumps({"board": board}, indent=2, ensure_ascii=False))


def cmd_readback():
    out = readback_words(DIAG_ADDR, DIAG_WORDS)
    snap = interpret_snapshot(words_from_mdw(out))
    print(json.dumps(snap, indent=2, ensure_ascii=False))
    return 0 if snap["valid"] else 3


def cmd_run(args):
    if not args.checklist or not os.path.exists(args.checklist):
        print("INCOMPLETE: physical checklist is required for a run (HITL gate)")
        return 3
    ok, reason = _checklist_ok(args.checklist)
    if not ok:
        print("INCOMPLETE: %s" % reason)
        return 3

    board = stlink_probe()  # physical operation; gate passed
    try:
        flash_sensing()
    except Exception as exc:  # noqa: BLE001
        print("FLASH FAIL: %s" % exc)
        return 4

    # Bounded observation: wait half, snapshot A, wait half, snapshot B.
    half = args.duration / 2.0
    time.sleep(half)
    text_a = readback_words(DIAG_ADDR, DIAG_WORDS)
    time.sleep(half)
    text_b = readback_words(DIAG_ADDR, DIAG_WORDS)
    snap_a = interpret_snapshot(words_from_mdw(text_a))
    snap_b = interpret_snapshot(words_from_mdw(text_b))
    if not snap_a["valid"] or not snap_b["valid"]:
        print("INCOMPLETE: invalid snapshot: %s %s"
              % (snap_a.get("error"), snap_b.get("error")))
        return 3
    verdict = full_verdict(snap_a, snap_b, args.duration)

    out_dir = args.out_dir or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "out",
        "sensing-proto-%s" % time.strftime("%Y%m%d-%H%M%S"))
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "board.json"), "w", encoding="utf-8") as f:
        json.dump({"board": board}, f, indent=2, ensure_ascii=False)
    with open(os.path.join(out_dir, "snapshot-a.json"), "w", encoding="utf-8") as f:
        json.dump(snap_a, f, indent=2, ensure_ascii=False)
    with open(os.path.join(out_dir, "snapshot-b.json"), "w", encoding="utf-8") as f:
        json.dump(snap_b, f, indent=2, ensure_ascii=False)
    with open(os.path.join(out_dir, "result.json"), "w", encoding="utf-8") as f:
        json.dump(verdict, f, indent=2, ensure_ascii=False)
    with open(os.path.join(out_dir, "raw-a.mdw"), "w", encoding="utf-8") as f:
        f.write(text_a)
    with open(os.path.join(out_dir, "raw-b.mdw"), "w", encoding="utf-8") as f:
        f.write(text_b)

    print(json.dumps(verdict, indent=2, ensure_ascii=False))
    print("evidence: %s" % out_dir)
    return 0 if verdict["pass"] else 1


def cmd_verdict(args):
    with open(args.from_a, "r", encoding="utf-8") as f:
        text_a = f.read()
    with open(args.from_b, "r", encoding="utf-8") as f:
        text_b = f.read()
    snap_a = interpret_snapshot(words_from_mdw(text_a))
    snap_b = interpret_snapshot(words_from_mdw(text_b))
    if not snap_a["valid"] or not snap_b["valid"]:
        print("INCOMPLETE: invalid snapshot: %s %s"
              % (snap_a.get("error"), snap_b.get("error")))
        return 3
    verdict = full_verdict(snap_a, snap_b, args.window)
    print(json.dumps(verdict, indent=2, ensure_ascii=False))
    return 0 if verdict["pass"] else 1


def main(argv=None):
    p = argparse.ArgumentParser(description="Sensing prototype bench CLI (#63)")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("probe")
    sub.add_parser("readback")
    run_p = sub.add_parser("run")
    run_p.add_argument("--duration", type=float, default=10.0)
    run_p.add_argument("--checklist", default=None)
    run_p.add_argument("--out-dir", default=None)
    v_p = sub.add_parser("verdict")
    v_p.add_argument("--from-a", required=True)
    v_p.add_argument("--from-b", required=True)
    v_p.add_argument("--window", type=float, required=True)

    args = p.parse_args(argv)
    if args.cmd == "probe":
        cmd_probe()
    elif args.cmd == "readback":
        return cmd_readback()
    elif args.cmd == "run":
        return cmd_run(args)
    elif args.cmd == "verdict":
        return cmd_verdict(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
