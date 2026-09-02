#!/usr/bin/env python3
"""PROTOTYPE - network_bridge transport layer model (ticket #75).

Throwaway host model answering the #75 design questions BEFORE production
C++ lands. NOT production code. Answers:

  D1  Byte-stream frame assembler: how does a partial/resync/gap stream
      split into canonical frames (0xE3 0x10, MTU 128, CRC16-CCITT-FALSE),
      and what link counters fall out of it (resyncs, drops, budget hits)?
  D2  RX byte budget split: 230 B/tick total RX+TX (#48 section 7) - what
      RX share keeps the assembler live without starving the TX drain, and
      how does back-pressure behave when the peer floods?
  D3  Handshake machine: Hello -> HelloAck/HandshakeReject, epoch fencing,
      protocolMajor check, expectedProfileId mismatch -> ProfileMismatch,
      bridgePrincipalHandle allocation (epoch-scoped, no-reassign) ->
      authorityId + grant. What are the failure paths?
  D4  Reconnect/gap: what does the controller observe on a link that goes
      quiet mid-frame and returns? (partial frame discarded by timeout,
      epoch refresh forced, principal map survives in-epoch).

Run host tests: unittest discover -s bench/transport-proto/tests -t .
Run CLI verdicts: python transport_logic.py demo
"""

from __future__ import annotations

import json

# ---------------------------------------------------------------------------
# Canonical frame constants (domain/codec.h, #47 section 4.1)
# ---------------------------------------------------------------------------
SYNC = bytes((0xE3, 0x10))
PROTO_MAJOR = 1
HEADER_LEN = 8
CRC_LEN = 2
MTU = 128
MAX_PAYLOAD = 116  # MTU - sync - header - crc
OVERHEAD = len(SYNC) + HEADER_LEN + CRC_LEN  # 12

FLAG_RESERVE = 0x01  # stop/handshake reserve slot (#43 section 6)

FAMILY_HANDSHAKE = 1
FAMILY_CONTROL = 2
FAMILY_SERVICE = 3
FAMILY_UPDATE = 4
FAMILY_SESSION = 5
FAMILY_OBSERVABILITY = 6
FAMILY_OUTCOME = 7

MSG_HS_HELLO = 0
MSG_HS_HELLO_ACK = 1
MSG_HS_REJECT = 2

QUEUE_CONTROL = 0
QUEUE_SERVICE = 1
QUEUE_UPDATE = 2

REJECT_EPOCH_MISMATCH = 0
REJECT_HANDSHAKE_REQUIRED = 1
REJECT_UNAUTHORIZED = 2
REJECT_ROLE_ESCALATION = 3
REJECT_UNSUPPORTED_VERSION = 4
REJECT_INVALID_ENVELOPE = 6
REJECT_PROFILE_MISMATCH = 14
REJECT_BUSY_REJECTED = 17

PROFILE_NETWORK_BRIDGE = 0
PROFILE_RADIO = 1

ROLE_CONTROL_CLIENT = 0x01
ROLE_SERVICE_CLIENT = 0x02

# Budget model (#48 section 7: 230 B/tick RX+TX total; #49 section 10:
# per-class TX caps 128 B, priority drain).
RX_BUDGET_PER_TICK = 115  # D2 candidate: half of 230
TX_BUDGET_PER_TICK = 115
INTER_FRAME_GAP_MS = 250  # V1 parser reset value (research C02)

MAX_PRINCIPALS = 16  # authorityId budget (#48 section 6)


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (mirrors domain/codec.cpp for the prototype)."""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------------------
# Wire encode/decode helpers (little-endian, mirrors the C++ codec)
# ---------------------------------------------------------------------------
def wr16(v: int) -> bytes:
    return bytes((v & 0xFF, (v >> 8) & 0xFF))


def wr32(v: int) -> bytes:
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF))


def rd16(b: bytes, o: int) -> int:
    return b[o] | (b[o + 1] << 8)


def rd32(b: bytes, o: int) -> int:
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def build_frame(family: int, msg_type: int, queue_class: int, payload: bytes,
                frame_seq: int = 0, flags: int = 0) -> bytes:
    """Canonical frame: sync + header + payload + crc16 (over header+payload)."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    hdr = bytes((PROTO_MAJOR, family, msg_type, queue_class, flags, frame_seq)) + wr16(len(payload))
    body = hdr + payload
    return SYNC + body + wr16(crc16_ccitt_false(body))


def frame_payload(rx_buf: bytes, start: int) -> bytes:
    """Payload bytes of the frame at start (header validated by caller)."""
    plen = rd16(rx_buf, start + 2 + 6)
    return rx_buf[start + 2 + HEADER_LEN:start + 2 + HEADER_LEN + plen]


def frame_total_len(rx_buf: bytes, start: int) -> int:
    plen = rd16(rx_buf, start + 2 + 6)
    return 2 + HEADER_LEN + plen + CRC_LEN


# ---------------------------------------------------------------------------
# D1: Assembler - byte stream -> canonical frames, resync on garbage.
# ---------------------------------------------------------------------------
class AssemblerStats:
    def __init__(self):
        self.frames_ok = 0
        self.resyncs = 0
        self.dropped_bytes = 0  # garbage consumed between frames
        self.bad_crc = 0
        self.truncated_flushes = 0  # partial frames discarded by timeout
        self.rx_bytes_budget_blocked = 0

    def to_dict(self):
        return self.__dict__.copy()


class Assembler:
    """Byte-stream frame assembler.

    Mirrors the D1 answer: state is ONE buffer + fill level, scanning is
    resync-on-bad-sync (scan forward to the next 0xE3, then check 0x10),
    each complete frame is CRC-verified before delivery. A frame whose
    payload_len exceeds MAX_PAYLOAD is treated as garbage (resync) - the
    wire length is never trusted (R1/R4).
    """

    def __init__(self):
        self.buf = bytearray()
        self.stats = AssemblerStats()

    def feed(self, data: bytes):
        """Feed raw RX bytes (caller applies the per-tick budget)."""
        self.buf += data

    def gap_timeout(self):
        """Inter-frame gap timer fired (250 ms without new bytes): a partial
        frame is discarded (counter), buffer otherwise kept."""
        if self.buf and not self._frame_complete_at(0):
            self.stats.truncated_flushes += 1
            self.buf = bytearray()

    def drain(self):
        """Pop complete valid frames (resync on garbage). Returns a list of
        (family, msg_type, queue_class, flags, payload) tuples."""
        out = []
        while True:
            frame = self._extract_one()
            if frame is None:
                break
            out.append(frame)
        return out

    def _frame_complete_at(self, start: int) -> bool:
        if len(self.buf) - start < OVERHEAD:
            return False
        plen = rd16(self.buf, start + 2 + 6)
        total = 2 + HEADER_LEN + plen + CRC_LEN
        return len(self.buf) - start >= total

    def _extract_one(self):
        # Need at least sync + header to decide anything.
        if len(self.buf) < OVERHEAD:
            return None
        # Resync: find the next sync pair.
        idx = 0
        found = False
        while idx + 1 < len(self.buf):
            if self.buf[idx] == SYNC[0] and self.buf[idx + 1] == SYNC[1]:
                found = True
                break
            idx += 1
        if not found:
            # No sync at all: keep the last byte (could be a sync start).
            garbage = len(self.buf) - 1
            if garbage > 0:
                self.stats.dropped_bytes += garbage
                self.stats.resyncs += 1
                del self.buf[:garbage]
            return None
        if idx > 0:
            self.stats.dropped_bytes += idx
            self.stats.resyncs += 1
            del self.buf[:idx]
        # Header in hand: check bounds before trusting plen.
        plen = rd16(self.buf, 2 + 6)
        if plen > MAX_PAYLOAD:
            # Garbage sync pair: drop one byte, rescan (never trust the
            # wire length).
            self.stats.dropped_bytes += 1
            self.stats.resyncs += 1
            del self.buf[:1]
            return self._extract_one()
        total = 2 + HEADER_LEN + plen + CRC_LEN
        if len(self.buf) < total:
            return None  # partial: wait for more bytes
        body = bytes(self.buf[2:2 + HEADER_LEN + plen])
        crc_wire = rd16(self.buf, 2 + HEADER_LEN + plen)
        del self.buf[:total]
        if crc16_ccitt_false(body) != crc_wire:
            self.stats.bad_crc += 1
            return self._extract_one()  # frame dropped, keep scanning
        family = body[1]
        msg_type = body[2]
        queue_class = body[3]
        flags = body[4]
        payload = body[HEADER_LEN:]
        self.stats.frames_ok += 1
        return (family, msg_type, queue_class, flags, payload)


# ---------------------------------------------------------------------------
# D3/D4: Handshake machine + principal registry (epoch-scoped).
# ---------------------------------------------------------------------------
class Principal:
    def __init__(self, authority_id, handle, roles, epoch):
        self.authority_id = authority_id
        self.handle = handle
        self.roles = roles
        self.epoch = epoch
        self.handshaked = True


class HandshakeStats:
    def __init__(self):
        self.hello_received = 0
        self.hello_acks = 0
        self.rejects = {}
        self.principals_allocated = 0
        self.budget_exhausted_rejects = 0

    def to_dict(self):
        return {
            "helloReceived": self.hello_received,
            "helloAcks": self.hello_acks,
            "rejects": dict(self.rejects),
            "principalsAllocated": self.principals_allocated,
            "budgetExhaustedRejects": self.budget_exhausted_rejects,
        }


class BridgeTransport:
    """Controller-side network_bridge link model.

    effective profile is fixed: network_bridge (ingress-derived, #47 5.1).
    The machine answers Hello with HelloAck (grant) or HandshakeReject with
    a stable code; principals are keyed by bridgePrincipalHandle and NEVER
    reassigned within one epoch (#47 5.1 p.5).
    """

    def __init__(self, epoch=1, controller_roles_grant=ROLE_CONTROL_CLIENT | ROLE_SERVICE_CLIENT):
        self.epoch = epoch
        self.controller_roles_grant = controller_roles_grant
        self.principals = {}  # handle -> Principal
        self.next_authority_id = 1  # 0 reserved
        self.stats = HandshakeStats()
        self.out_frames = []  # (family, msg_type, queue_class, payload)

    def on_epoch_change(self, new_epoch):
        """D4: epoch change clears the handle->authority map (#47 5.1 p.5)."""
        self.epoch = new_epoch
        self.principals = {}
        self.next_authority_id = 1

    def resolve(self, handle):
        p = self.principals.get(handle)
        return p if p and p.epoch == self.epoch else None

    def _emit(self, family, msg_type, queue_class, payload):
        self.out_frames.append((family, msg_type, queue_class, payload))

    def on_frame(self, family, msg_type, queue_class, flags, payload, handle):
        """Route ONE decoded frame by family. Returns None (observable
        emissions are the out_frames list + stats)."""
        if family == FAMILY_HANDSHAKE:
            if msg_type == MSG_HS_HELLO:
                self._on_hello(payload, handle)
                return
            # Unknown handshake message: reject InvalidEnvelope.
            self._reject(handle, REJECT_INVALID_ENVELOPE, family, msg_type)
            return
        # Non-handshake family: principal must be resolved (post-handshake
        # invariant, #47 5.1 p.6).
        p = self.resolve(handle)
        if p is None:
            self._reject(handle, REJECT_HANDSHAKE_REQUIRED, family, msg_type)
            return
        # Delivered to the semantic layer (prototype: counters only).
        self._emit(FAMILY_CONTROL, 0x7F, QUEUE_CONTROL, b"")  # marker, host-only

    def _on_hello(self, payload, handle):
        self.stats.hello_received += 1
        # Hello payload layout (D3 candidate): protoMajor u8,
        # expectedProfileId u8, bridgePrincipalHandle u16, requestedRoles u8.
        if len(payload) < 5:
            self._reject(handle, REJECT_INVALID_ENVELOPE, FAMILY_HANDSHAKE, MSG_HS_HELLO)
            return
        proto_major = payload[0]
        expected_profile = payload[1]
        hello_handle = rd16(payload, 2)
        requested_roles = payload[4]
        if proto_major != PROTO_MAJOR:
            self._reject(handle, REJECT_UNSUPPORTED_VERSION, FAMILY_HANDSHAKE, MSG_HS_HELLO)
            return
        if expected_profile != PROFILE_NETWORK_BRIDGE:
            self._reject(handle, REJECT_PROFILE_MISMATCH, FAMILY_HANDSHAKE, MSG_HS_HELLO)
            return
        # Cross-principal spoof: the transport handle of the frame must
        # match the hello handle (#47 5.1 p.6).
        if handle is not None and handle != hello_handle:
            self._reject(handle, REJECT_UNAUTHORIZED, FAMILY_HANDSHAKE, MSG_HS_HELLO)
            return
        # Re-hello of a known in-epoch handle: same authority (#47 p.5).
        known = self.principals.get(hello_handle)
        if known is not None and known.epoch == self.epoch:
            grant = known.roles & self.controller_roles_grant
            self._hello_ack(hello_handle, known.authority_id, grant)
            return
        # New principal: allocate or BusyRejected.
        if len(self.principals) >= MAX_PRINCIPALS:
            self.stats.budget_exhausted_rejects += 1
            self._reject_handle(hello_handle, REJECT_BUSY_REJECTED)
            return
        roles = requested_roles & self.controller_roles_grant  # grant subset
        p = Principal(self.next_authority_id, hello_handle, roles, self.epoch)
        self.next_authority_id += 1
        self.principals[hello_handle] = p
        self.stats.principals_allocated += 1
        self._hello_ack(hello_handle, p.authority_id, roles)

    def _hello_ack(self, handle, authority_id, granted_roles):
        # HelloAck payload: controllerEpoch u32, authorityId u16,
        # grantedRoles u8, effectiveProfileId u8, capabilities u16 (reserved 0).
        payload = wr32(self.epoch) + wr16(authority_id) + bytes((granted_roles, PROFILE_NETWORK_BRIDGE)) + wr16(0)
        self.stats.hello_acks += 1
        self._emit(FAMILY_HANDSHAKE, MSG_HS_HELLO_ACK, QUEUE_CONTROL, payload)

    def _reject(self, handle, code, family, msg_type):
        self._reject_handle(handle, code)

    def _reject_handle(self, handle, code):
        self.stats.rejects[code] = self.stats.rejects.get(code, 0) + 1
        payload = bytes((code,))
        self._emit(FAMILY_HANDSHAKE, MSG_HS_REJECT, QUEUE_CONTROL, payload)


# ---------------------------------------------------------------------------
# D2: RX budget pump - bounded RX per tick, flood back-pressure.
# ---------------------------------------------------------------------------
class RxPump:
    """Per-tick RX byte pump feeding the assembler.

    D2 answer model: RX gets a fixed share of the 230 B/tick link budget;
    unconsumed bytes STAY in the (bounded) driver ring. The model exposes
    whether the ring overflows (peer flood) - the production contract will
    drop with a counter rather than block.
    """

    def __init__(self, assembler: Assembler, budget=RX_BUDGET_PER_TICK, ring=256):
        self.assembler = assembler
        self.budget = budget
        self.ring = bytearray()
        self.ring_cap = ring
        self.overflow_events = 0

    def on_bytes(self, data: bytes):
        """ISR-side: bytes land in the bounded ring; overflow is DROPPED
        (counter, never block)."""
        room = self.ring_cap - len(self.ring)
        if len(data) > room:
            self.overflow_events += 1
            data = data[:room]
        self.ring += data

    def tick(self):
        """Foreground-side: move up to `budget` bytes ring -> assembler."""
        take = min(self.budget, len(self.ring))
        if take:
            self.assembler.feed(bytes(self.ring[:take]))
            del self.ring[:take]
        return take


# ---------------------------------------------------------------------------
# Demo verdicts (OFF-bench, fixtures) - CLI proof of D1-D4.
# ---------------------------------------------------------------------------
def demo_cases():
    cases = []

    # D1: clean stream of 3 frames -> 3 frames, 0 resyncs.
    asm = Assembler()
    frames = [build_frame(FAMILY_CONTROL, 0, QUEUE_CONTROL, bytes((i,))) for i in range(3)]
    stream = b"".join(frames)
    asm.feed(stream)
    out = asm.drain()
    cases.append(("D1-clean", len(out) == 3 and asm.stats.resyncs == 0, asm.stats.to_dict()))

    # D1: garbage between frames -> resync, frames survive.
    asm = Assembler()
    noisy = frames[0] + b"\x00\x01\x02garbage" + frames[1] + b"\xff" + frames[2]
    asm.feed(noisy)
    out = asm.drain()
    ok = len(out) == 3 and asm.stats.resyncs >= 2
    cases.append(("D1-resync", ok, asm.stats.to_dict()))

    # D1: partial frame then the rest -> one frame.
    asm = Assembler()
    half = frames[0][:8]
    asm.feed(half)
    out = asm.drain()
    asm.feed(frames[0][8:])
    out += asm.drain()
    cases.append(("D1-partial", len(out) == 1, asm.stats.to_dict()))

    # D1: corrupted CRC -> frame dropped, next survives.
    asm = Assembler()
    corrupt = bytearray(frames[0])
    corrupt[-1] ^= 0xFF
    asm.feed(bytes(corrupt) + frames[1])
    out = asm.drain()
    ok = len(out) == 1 and asm.stats.bad_crc == 1
    cases.append(("D1-badcrc", ok, asm.stats.to_dict()))

    # D1: oversized payload_len in header -> garbage resync, not a hang.
    asm = Assembler()
    evil = SYNC + bytes((PROTO_MAJOR, FAMILY_CONTROL, 0, QUEUE_CONTROL, 0, 0)) + wr16(0xFFFF) + b"\x00" * 10
    asm.feed(evil + frames[0])
    out = asm.drain()
    cases.append(("D1-evil-len", len(out) == 1, asm.stats.to_dict()))

    # D2: flood larger than ring -> overflow counter, no block.
    asm = Assembler()
    pump = RxPump(asm)
    pump.on_bytes(b"\xA5" * 1024)
    for _ in range(4):
        pump.tick()
    out = asm.drain()
    ok = pump.overflow_events == 1 and len(out) == 0
    cases.append(("D2-flood", ok, {"rxPump": True, "assembler": asm.stats.to_dict()}))

    # D2: steady frames arrive across ticks despite budget.
    asm = Assembler()
    pump = RxPump(asm)
    pump.on_bytes(stream)
    for _ in range(5):
        pump.tick()
    out = asm.drain()
    cases.append(("D2-budgeted", len(out) == 3, asm.stats.to_dict()))

    # D3: happy handshake -> HelloAck with authorityId 1.
    t = BridgeTransport(epoch=1)
    hello = bytes((PROTO_MAJOR, PROFILE_NETWORK_BRIDGE)) + wr16(0x0102) + bytes((ROLE_CONTROL_CLIENT,))
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello, handle=0x0102)
    ack = [f for f in t.out_frames if f[1] == MSG_HS_HELLO_ACK]
    ok = bool(ack) and rd16(ack[0][3], 4) == 1
    cases.append(("D3-hello-ack", ok, t.stats.to_dict()))

    # D3: wrong expectedProfileId -> ProfileMismatch.
    t = BridgeTransport(epoch=1)
    hello_bad = bytes((PROTO_MAJOR, PROFILE_RADIO)) + wr16(0x0102) + bytes((ROLE_CONTROL_CLIENT,))
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello_bad, handle=0x0102)
    ok = t.stats.rejects.get(REJECT_PROFILE_MISMATCH) == 1
    cases.append(("D3-profile-mismatch", ok, t.stats.to_dict()))

    # D3: wrong protoMajor -> UnsupportedVersion.
    t = BridgeTransport(epoch=1)
    hello_v2 = bytes((2, PROFILE_NETWORK_BRIDGE)) + wr16(0x0102) + bytes((ROLE_CONTROL_CLIENT,))
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello_v2, handle=0x0102)
    ok = t.stats.rejects.get(REJECT_UNSUPPORTED_VERSION) == 1
    cases.append(("D3-major-mismatch", ok, t.stats.to_dict()))

    # D3: re-hello same handle -> SAME authorityId.
    t = BridgeTransport(epoch=1)
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello, handle=0x0102)
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello, handle=0x0102)
    acks = [f for f in t.out_frames if f[1] == MSG_HS_HELLO_ACK]
    ok = len(acks) == 2 and rd16(acks[0][3], 4) == rd16(acks[1][3], 4)
    cases.append(("D3-rehello-same-authority", ok, t.stats.to_dict()))

    # D3: 17th principal -> BusyRejected.
    t = BridgeTransport(epoch=1)
    for i in range(MAX_PRINCIPALS + 1):
        h = bytes((PROTO_MAJOR, PROFILE_NETWORK_BRIDGE)) + wr16(0x100 + i) + bytes((ROLE_CONTROL_CLIENT,))
        t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, h, handle=0x100 + i)
    ok = t.stats.budget_exhausted_rejects == 1 and t.stats.principals_allocated == MAX_PRINCIPALS
    cases.append(("D3-budget-exhausted", ok, t.stats.to_dict()))

    # D3: pre-handshake control frame -> HandshakeRequired, no marker out.
    t = BridgeTransport(epoch=1)
    t.on_frame(FAMILY_CONTROL, 0, QUEUE_CONTROL, 0, b"", handle=0x0102)
    ok = t.stats.rejects.get(REJECT_HANDSHAKE_REQUIRED) == 1
    cases.append(("D3-pre-handshake", ok, t.stats.to_dict()))

    # D4: epoch change clears principals; re-hello allocates fresh.
    t = BridgeTransport(epoch=1)
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello, handle=0x0102)
    t.on_epoch_change(2)
    t.on_frame(FAMILY_CONTROL, 0, QUEUE_CONTROL, 0, b"", handle=0x0102)  # old map gone
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, hello, handle=0x0102)
    acks = [f for f in t.out_frames if f[1] == MSG_HS_HELLO_ACK]
    ok = t.stats.rejects.get(REJECT_HANDSHAKE_REQUIRED) == 1 and rd16(acks[-1][3], 4) == 1
    cases.append(("D4-epoch-refresh", ok, t.stats.to_dict()))

    # D4: partial frame + gap timeout -> truncated flush.
    asm = Assembler()
    asm.feed(frames[0][:8])
    asm.gap_timeout()
    out = asm.drain()
    ok = len(out) == 0 and asm.stats.truncated_flushes == 1
    cases.append(("D4-gap-flush", ok, asm.stats.to_dict()))

    # D4: spoof - frame handle != hello handle -> Unauthorized.
    t = BridgeTransport(epoch=1)
    spoof_hello = bytes((PROTO_MAJOR, PROFILE_NETWORK_BRIDGE)) + wr16(0x0BAD) + bytes((ROLE_CONTROL_CLIENT,))
    t.on_frame(FAMILY_HANDSHAKE, MSG_HS_HELLO, QUEUE_CONTROL, FLAG_RESERVE, spoof_hello, handle=0x0102)
    ok = t.stats.rejects.get(REJECT_UNAUTHORIZED) == 1
    cases.append(("D4-spoof", ok, t.stats.to_dict()))

    return cases


def main():
    cases = demo_cases()
    results = []
    for name, ok, stats in cases:
        results.append({"case": name, "pass": bool(ok), "stats": stats})
    verdict = all(r["pass"] for r in results)
    doc = {
        "prototype": "transport-proto",
        "ticket": 75,
        "verdict": "PASS" if verdict else "FAIL",
        "cases": results,
    }
    print(json.dumps(doc, indent=2))
    return 0 if verdict else 1


if __name__ == "__main__":
    raise SystemExit(main())
