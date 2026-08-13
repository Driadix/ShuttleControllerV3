"""Bounded UART capture via pyserial (ticket #65).

Bounded window (durationS) with a maxBytes guard; the raw stream is the
primary evidence artifact of the bundle (issue #60 section 2.4).
"""

import time
from pathlib import Path

import serial

DEFAULT_BAUD = 230400
DEFAULT_PARITY = "E"  # 8E1 (network_bridge display profile)

_PARITY = {"N": serial.PARITY_NONE, "E": serial.PARITY_EVEN,
           "O": serial.PARITY_ODD}


def capture(port: str, duration_s: float, baud: int = DEFAULT_BAUD,
            parity: str = DEFAULT_PARITY, max_bytes: int = 2_000_000) -> bytes:
    """Bounded read of the serial stream; returns raw bytes."""
    with serial.Serial(port, baud, parity=_PARITY.get(parity.upper(), serial.PARITY_EVEN),
                       stopbits=1, timeout=0.2) as s:
        s.reset_input_buffer()
        buf = bytearray()
        deadline = time.time() + duration_s
        while time.time() < deadline:
            chunk = s.read(s.in_waiting or 1)
            if chunk:
                buf.extend(chunk)
                if len(buf) >= max_bytes:
                    break
    return bytes(buf)


def run_capture(scenario: dict, port: str, out_dir: Path):
    """Capture per scenario config, write the raw artifact.

    Returns (raw, raw_path) where raw_path is POSIX-style (owner decision,
    design doc section 0.4).
    """
    cap = scenario["capture"]
    raw = capture(port, float(cap["durationS"]), int(cap["baud"]),
                  cap.get("parity", DEFAULT_PARITY),
                  int(cap.get("maxBytes", 2_000_000)))
    raw_path = out_dir / f"raw-{scenario['id']}.bin"
    raw_path.write_bytes(raw)
    return raw, raw_path.as_posix()
