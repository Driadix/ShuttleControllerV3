#!/usr/bin/env python3
"""PROTOTYPE fixture generator (ticket #60) - synthetic raw captures.

Produces synthetic raw UART captures so the operator-loop pipeline mechanics
(normalize -> oracle -> verdict -> evidence) can be demonstrated without the
bench. Frame format mirrors bench/bridge-relay/tools/capture.py (0xBB 0xCC
header, CRC16-CCITT LSB-first).

    python gen_fixture.py            # writes fixtures/*.bin
"""

from __future__ import annotations

import struct
from pathlib import Path

HDR = bytes((0xBB, 0xCC))
OUT = Path(__file__).parent / "fixtures"


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(msg_id: int, payload: bytes, seq: int = 0) -> bytes:
    body = HDR + bytes((msg_id, 0, seq & 0xFF, len(payload))) + payload
    c = crc16(body)
    return body + bytes((c & 0xFF, (c >> 8) & 0xFF))


def sensors_payload(dist_chr=1500, dist_plf=180, valid=True) -> bytes:
    v = 1 if valid else 0
    # 4 roles x {dist u16 LE, valid u8, pad u8} = 16 B (capture.py layout)
    vals = [1500 + 0, v, 0, 0, 0, 0, 180 + 0, v]
    return struct.pack("<" + "HBx" * 4, *vals)


def as5600_payload(angle=1234) -> bytes:
    # 21 B As5600HealthPacket (little-endian angle + flags/counters)
    return struct.pack("<H", angle) + bytes(19)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)

    # bench-alive: 10 sensor + 5 health + 2 log frames, all CRC-valid
    alive = bytearray()
    for i in range(10):
        alive += frame(0x02, sensors_payload(1500 + i, 132 + i * 5), seq=i)
        alive += frame(0x09, as5600_payload(1234 + i), seq=i)
    alive += frame(0x10, b"boot: Init encoder success", seq=100)
    alive += frame(0x10, b"kernel: ready", seq=101)
    (OUT / "bench-alive.bin").write_bytes(bytes(alive))

    # silent: empty capture (e.g. kernel Phase-1 emits nothing on UART)
    (OUT / "silent.bin").write_bytes(b"")

    # noisy: valid frames + many corrupted frames (bad CRC ratio > 0.2)
    noisy = bytearray(alive)
    for k in range(12):
        noisy += b"\xBB\xCC\x10\x00\x00\x05" + b"junk" * 4 + b"\x00\x00"  # bad CRC
    (OUT / "noisy.bin").write_bytes(bytes(noisy))

    for f in sorted(OUT.iterdir()):
        print(f"{f.name}: {f.stat().st_size} B")


if __name__ == "__main__":
    main()
