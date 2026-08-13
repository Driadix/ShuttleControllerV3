#!/usr/bin/env python3
"""Capture and decode the L4 bench controller UART stream (ticket #73).

Reads raw bytes from the bench log channel (default: USB CDC of the
LilyGo T-Display S3 relay on COM9) and decodes the shuttle protocol
frames: header 0xBB 0xCC + msgID/targetID/seq/length, payload, then
CRC16-CCITT (init 0xFFFF, polynomial 0x1021) stored LSB first.

Usage:
    python capture.py [PORT] [DURATION_SECONDS]

Examples:
    python capture.py COM9 20
    python capture.py COM29 10

Output:
    - raw byte count
    - frame count, CRC-valid / CRC-bad
    - per-msgID frame counts
    - MSG_LOG (0x10) text lines
    - MSG_SENSORS (0x02) per-role distances + VALID bits
    - named decode for MSG_AS5600_HEALTH (0x09) when present

Exit code: 0 only when at least one CRC-valid frame was captured.

This is bench tooling for the L4 sensor bench; see
docs/l4-sensor-bench-v3.md and bench/bridge-relay/README.md.
"""

import sys
import time

import serial

HDR_LEN = 6        # sync1 sync2 msgID targetID seq length
MAX_PAYLOAD = 120  # canonical frame 128 = HDR + payload + CRC
MAX_FRAME = HDR_LEN + MAX_PAYLOAD + 2

MSG_NAMES = {
    0x01: "HEARTBEAT",
    0x02: "SENSORS",
    0x03: "STATS",
    0x07: "LINK_HEALTH",
    0x09: "AS5600_HEALTH",
    0x10: "MSG_LOG",
    0x11: "MSG_LOG_RAW_TEXT",
    0x22: "CONFIG_REP",
    0x25: "CONFIG_SYNC_REP",
    0x33: "ACK",
}

# SensorPacket roles: ChR=0x09<-distR, ChF=0x0A<-distF, PlR=0x0B<-distPltR,
# PlF=0x0C<-distPltF (Cntrl_V2 ShuttleProtocol.h)
SENSOR_ROLES = (
    ("0x09 ChR", 2, 1 << 11),   # distR offset, HW_FLAG_TOF_CH_R_VALID
    ("0x0A ChF", 0, 1 << 10),   # distF offset, HW_FLAG_TOF_CH_F_VALID
    ("0x0B PlR", 6, 1 << 13),   # distPltR offset, HW_FLAG_TOF_PAL_R_VALID
    ("0x0C PlF", 4, 1 << 12),   # distPltF offset, HW_FLAG_TOF_PAL_F_VALID
)


def decode_sensors(payload: bytes) -> str:
    if len(payload) != 16:
        return f"sensors_bad_len({len(payload)}B)"
    hw_flags = int.from_bytes(payload[14:16], "little")
    parts = []
    for name, off, flag in SENSOR_ROLES:
        dist = int.from_bytes(payload[off : off + 2], "little")
        valid = (hw_flags & flag) != 0
        parts.append(f"{name}={'%u' % dist if valid else 'invalid'}")
    return ", ".join(parts)


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def decode_as5600(payload: bytes) -> str:
    # As5600HealthPacket is exactly 21 B, little-endian (ShuttleProtocol.h)
    if len(payload) != 21:
        return f"as5600_bad_len({len(payload)}B)"
    age_ms = int.from_bytes(payload[0:4], "little")
    failures = int.from_bytes(payload[4:8], "little")
    angle_raw = int.from_bytes(payload[8:10], "little")
    magnitude = int.from_bytes(payload[10:12], "little")
    recovery = int.from_bytes(payload[12:14], "little")
    state = payload[14]
    flags = payload[15]
    status = payload[16]
    agc = payload[17]
    fail = payload[18]
    succ = payload[19]
    i2c = payload[20]
    flag_names = []
    for bit, name in ((0, "RESP"), (1, "ANGLE"), (2, "MAG"), (3, "WEAK"),
                      (4, "STRONG"), (5, "FAULT")):
        if flags & (1 << bit):
            flag_names.append(name)
    return (
        f"age={age_ms}ms fail={failures} raw={angle_raw} mag={magnitude} "
        f"rec={recovery} st={state:#04x} fl={','.join(flag_names) or '-'} "
        f"sr={status:#04x} agc={agc} cFail={fail} cSucc={succ} i2c={i2c:#04x}"
    )


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

    s = serial.Serial(port, 230400, timeout=1)
    s.reset_input_buffer()
    t0 = time.time()
    buf = b""
    while time.time() - t0 < duration:
        chunk = s.read(2048)
        if chunk:
            buf += chunk
    s.close()

    print(f"port={port} duration={duration}s raw_bytes={len(buf)}")

    i = 0
    frames = ok = bad = 0
    msgs = {}
    logs = []
    as5600 = []
    sensors = []
    while i + HDR_LEN + 2 <= len(buf):
        if buf[i] != 0xBB or buf[i + 1] != 0xCC:
            i += 1
            continue
        length = buf[i + 5]
        if length > MAX_PAYLOAD:
            # corrupt length byte: resync one byte at a time
            i += 1
            continue
        total = HDR_LEN + length + 2
        if i + total > len(buf):
            break
        frame = buf[i : i + total]
        crc = crc16(frame[:-2])
        if (crc & 0xFF) == frame[-2] and ((crc >> 8) & 0xFF) == frame[-1]:
            ok += 1
            mid = frame[2]
            msgs[mid] = msgs.get(mid, 0) + 1
            payload = frame[HDR_LEN : HDR_LEN + length]
            if mid == 0x10:
                logs.append(payload[1:].split(b"\x00")[0].decode("latin1"))
            elif mid == 0x09 and len(as5600) < 3:
                as5600.append(decode_as5600(payload))
            elif mid == 0x02 and len(sensors) < 3:
                sensors.append(decode_sensors(payload))
        else:
            bad += 1
        frames += 1
        i += total

    print(f"frames={frames} crc_ok={ok} crc_bad={bad}")
    for mid, count in sorted(msgs.items()):
        print(f"  msgID 0x{mid:02x} {MSG_NAMES.get(mid, '?')}: {count}")
    for line in logs:
        print(f"  LOG: {line}")
    for s in sensors:
        print(f"  SENSORS: {s}")
    for a in as5600:
        print(f"  AS5600: {a}")
    return 0 if ok > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
