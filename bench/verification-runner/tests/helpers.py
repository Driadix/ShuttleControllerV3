"""Test helpers: synthetic frames and scenarios.

Frame building mirrors the prototype generator bench/operator-loop-proto/
gen_fixture.py (ticket #60); scenarios carry the required bench identity.
"""

import json
import struct
from pathlib import Path

HDR = bytes((0xBB, 0xCC))
MSG_LOG = 0x10
MSG_SENSORS = 0x02
MSG_AS5600_HEALTH = 0x09

BENCH_PART = "STM32F405RG"
BENCH_UID = "002900363033470336363131"


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 \
                else (crc << 1) & 0xFFFF
    return crc


def frame(msg_id: int, payload: bytes, seq: int = 0, corrupt: bool = False) -> bytes:
    body = HDR + bytes((msg_id, 0, seq & 0xFF, len(payload))) + payload
    c = crc16(body)
    if corrupt:
        c ^= 0xFFFF
    return body + bytes((c & 0xFF, (c >> 8) & 0xFF))


def sensors_payload(dist_chr: int = 1500, dist_plf: int = 180,
                    valid: bool = True) -> bytes:
    v = 1 if valid else 0
    vals = [dist_chr, v, 0, 0, 0, 0, dist_plf, v]
    return struct.pack("<" + "HBx" * 4, *vals)


def as5600_payload(angle: int = 1234) -> bytes:
    return struct.pack("<H", angle) + bytes(19)


def bench_alive() -> bytes:
    """22 valid frames: 10x (SENSORS + AS5600_HEALTH) + 2x LOG."""
    raw = bytearray()
    for i in range(10):
        raw += frame(MSG_SENSORS, sensors_payload(1500 + i, 132 + i * 5), seq=i)
        raw += frame(MSG_AS5600_HEALTH, as5600_payload(1234 + i), seq=i)
    raw += frame(MSG_LOG, b"boot: Init encoder success", seq=100)
    raw += frame(MSG_LOG, b"kernel: ready", seq=101)
    return bytes(raw)


def noisy() -> bytes:
    """bench_alive + 12 corrupted frames (bad CRC ratio > 0.2)."""
    raw = bytearray(bench_alive())
    for _ in range(12):
        raw += frame(MSG_LOG, b"junk" * 4, corrupt=True)
    return bytes(raw)


def scenario(**over) -> dict:
    """A valid behavior scenario; override top-level or nested keys."""
    sc = {
        "schemaVersion": 1, "id": "t-behavior", "title": "test behavior",
        "type": "behavior", "phase": "L4",
        "identity": {"board": {"part": BENCH_PART, "uid": BENCH_UID}},
        "flash": {"required": False, "env": "firmware"},
        "capture": {"port": "auto", "baud": 230400, "parity": "E",
                    "durationS": 1, "maxBytes": 100000},
        "oracle": {"minFrames": 22, "maxCrcBadRatio": 1.0,
                   "requirePatterns": [], "forbidPatterns": []},
    }
    sc.update(over)
    return sc


def flash_verify_scenario(**over) -> dict:
    sc = scenario(id="t-flash-verify", title="test flash-verify",
                  type="flash-verify",
                  flash={"required": True, "env": "firmware"},
                  oracle={"minFrames": 0, "maxCrcBadRatio": 1.0,
                          "requirePatterns": [], "forbidPatterns": []})
    sc.update(over)
    return sc


def probe_ok() -> dict:
    return {"probe": "PASS", "part": BENCH_PART,
            "idcode": "0x100f6413", "uid": BENCH_UID}


def signed_checklist(tmp: str, owner: str = "Driadix") -> str:
    """Write a signed owner checklist into tmp; returns its path."""
    cl = {"schemaVersion": 1, "signed": True, "owner": owner, "at": "now",
          "items": [{"text": "bench state attested", "confirmed": True}]}
    p = Path(tmp) / "checklist.json"
    p.write_text(json.dumps(cl), encoding="utf-8")
    return str(p)


def unsigned_checklist(tmp: str, owner: str = "Driadix") -> str:
    """Write an unsigned checklist into tmp; returns its path."""
    cl = {"schemaVersion": 1, "signed": False, "owner": owner, "at": "now",
          "items": [{"text": "bench state attested", "confirmed": False}]}
    p = Path(tmp) / "checklist.json"
    p.write_text(json.dumps(cl), encoding="utf-8")
    return str(p)
