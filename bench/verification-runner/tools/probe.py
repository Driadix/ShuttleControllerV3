"""Board and UART detection: OpenOCD probe (idcode + UID) and pyserial check.

Bench facts: DBGMCU_IDCODE at 0xE0042000, 96-bit UID at 0x1FFF7A10
(docs/l4-sensor-bench-v3.md, ticket #73). OpenOCD 12 prints data words
WITHOUT the 0x prefix - the parser handles both forms (ticket #60 gotcha).
"""

import os
import re
import subprocess
import time

import serial

OPENOCD = os.path.join(
    os.environ.get("USERPROFILE", ""),
    ".platformio", "packages", "tool-openocd", "bin", "openocd.exe",
)

DEFAULT_BAUD = 230400
DEFAULT_PARITY = "E"  # 8E1 (network_bridge display profile)

_PART_BY_DEV_ID = {
    0x413: "STM32F405RG",
    0x419: "STM32F427/437",
    0x411: "STM32F40x",
}


def kill_openocd():
    """Kill stale OpenOCD instances (Windows) - else LIBUSB_ERROR_ACCESS."""
    if os.name == "nt":
        subprocess.run(["taskkill", "/F", "/IM", "openocd.exe"],
                       capture_output=True, check=False)


def stlink_probe(timeout_s: int = 40) -> dict:
    """Probe the ST-Link: {probe, part, idcode, uid}. Raises on failure."""
    if not os.path.exists(OPENOCD):
        raise RuntimeError(f"openocd not found: {OPENOCD}")
    kill_openocd()
    cfg = [
        OPENOCD, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg",
        "-c", "init; halt; mdw 0xE0042000 1", "-c", "mdw 0x1FFF7A10 3",
        "-c", "resume",  # never leave the MCU halted (ticket #63 L1 gate fix)
        "-c", "shutdown",
    ]
    r = subprocess.run(cfg, capture_output=True, text=True, timeout=timeout_s)
    out = (r.stdout or "") + (r.stderr or "")
    m_id = re.search(r"0xe0042000:\s*([0-9a-fA-F]{8})", out)
    m_part = re.search(r"(\w+)\s+\(id\s+0x[0-9a-fA-F]+\)", out)
    m_uid = re.search(
        r"0x1fff7a10:\s*([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})", out)
    if not m_id:
        raise RuntimeError(f"ST-Link probe failed (no DBGMCU idcode): {out[-400:]}")
    dev_id = int(m_id.group(1), 16) & 0xFFF
    part = _PART_BY_DEV_ID.get(dev_id, f"STM32F4 dev_id=0x{dev_id:03X}")
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
               timeout_s: float = 2.0) -> dict:
    """Open the port and read a little; returns {port, open, bytes|error}."""
    par = {"N": serial.PARITY_NONE, "E": serial.PARITY_EVEN,
           "O": serial.PARITY_ODD}.get(parity.upper(), serial.PARITY_EVEN)
    try:
        with serial.Serial(port, baud, parity=par, stopbits=1,
                           timeout=timeout_s) as s:
            time.sleep(0.3)
            n = s.in_waiting
            data = s.read(min(n, 4096)) if n else b""
        return {"port": port, "open": True, "bytes": len(data)}
    except Exception as exc:  # noqa: BLE001 - port check must not raise
        return {"port": port, "open": False, "error": str(exc)}
