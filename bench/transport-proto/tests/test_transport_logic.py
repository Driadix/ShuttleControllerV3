"""Host tests for the transport prototype model (ticket #75). Run with the
bench venv: ../../.venv-pio312/Scripts/python.exe -m unittest discover
-s bench/transport-proto/tests -t .
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import transport_logic as tl  # noqa: E402


def hello_payload(handle, roles=tl.ROLE_CONTROL_CLIENT, major=tl.PROTO_MAJOR,
                  profile=tl.PROFILE_NETWORK_BRIDGE):
    return bytes((major, profile)) + tl.wr16(handle) + bytes((roles,))


def control_frame(msg_type=0, payload=b"", seq=0):
    return tl.build_frame(tl.FAMILY_CONTROL, msg_type, tl.QUEUE_CONTROL,
                          payload, frame_seq=seq)


class CrcMirrorTest(unittest.TestCase):
    def test_known_value(self):
        # CRC-16/CCITT-FALSE("123456789") = 0x29B1 (reference vector).
        self.assertEqual(tl.crc16_ccitt_false(b"123456789"), 0x29B1)

    def test_frame_crc_roundtrip(self):
        f = control_frame()
        body = f[2:-2]
        self.assertEqual(tl.crc16_ccitt_false(body), tl.rd16(f, len(f) - 2))


class AssemblerTest(unittest.TestCase):
    def test_empty_feed(self):
        asm = tl.Assembler()
        self.assertEqual(asm.drain(), [])
        self.assertEqual(asm.stats.frames_ok, 0)

    def test_single_sync_byte_garbage_flushed_with_next_frame(self):
        # A short garbage prefix stays buffered (under OVERHEAD, scan
        # deferred); once a real frame arrives, the prefix is counted and
        # dropped, the frame survives.
        f = control_frame(0, b"\x01")
        asm = tl.Assembler()
        asm.feed(b"\x00\x00\x00\xE3")
        self.assertEqual(asm.drain(), [])
        self.assertEqual(asm.stats.dropped_bytes, 0)  # scan deferred
        asm.feed(f)
        out = asm.drain()
        self.assertEqual(len(out), 1)
        self.assertEqual(asm.stats.dropped_bytes, 4)
        self.assertEqual(asm.stats.resyncs, 1)

    def test_frame_split_anywhere(self):
        f = control_frame(0, b"\x01\x02\x03")
        for cut in range(1, len(f)):
            asm = tl.Assembler()
            asm.feed(f[:cut])
            self.assertEqual(asm.drain(), [], f"cut {cut} must be partial")
            asm.feed(f[cut:])
            out = asm.drain()
            self.assertEqual(len(out), 1, f"cut {cut} must reassemble")
            self.assertEqual(out[0][4], b"\x01\x02\x03")

    def test_oversized_len_not_trusted(self):
        evil = tl.SYNC + bytes((tl.PROTO_MAJOR, tl.FAMILY_CONTROL, 0, tl.QUEUE_CONTROL, 0, 0)) \
            + tl.wr16(0xFFFF) + b"\x00" * 8
        asm = tl.Assembler()
        asm.feed(evil)
        # Garbage consumed, buffer not grown, no hang.
        self.assertEqual(asm.drain(), [])
        self.assertLessEqual(len(asm.buf), 2)

    def test_gap_timeout_keeps_full_frame(self):
        f = control_frame()
        asm = tl.Assembler()
        asm.feed(f)
        asm.gap_timeout()
        out = asm.drain()
        self.assertEqual(len(out), 1)
        self.assertEqual(asm.stats.truncated_flushes, 0)

    def test_bad_crc_chain(self):
        good = control_frame(1, b"aa")
        bad = bytearray(control_frame(2, b"bb"))
        bad[-2] ^= 0x55
        asm = tl.Assembler()
        asm.feed(bytes(bad) + good)
        out = asm.drain()
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0][1], 1)
        self.assertEqual(asm.stats.bad_crc, 1)


class RxPumpTest(unittest.TestCase):
    def test_tick_respects_budget(self):
        asm = tl.Assembler()
        pump = tl.RxPump(asm, budget=10)
        pump.on_bytes(b"\xA5" * 100)
        moved = pump.tick()
        self.assertEqual(moved, 10)
        self.assertEqual(len(pump.ring), 90)

    def test_overflow_counts_and_clips(self):
        asm = tl.Assembler()
        pump = tl.RxPump(asm, ring=16)
        pump.on_bytes(b"\x01" * 10)
        pump.on_bytes(b"\x02" * 10)  # only 6 fit
        self.assertEqual(pump.overflow_events, 1)
        self.assertEqual(len(pump.ring), 16)

    def test_empty_tick_is_noop(self):
        asm = tl.Assembler()
        pump = tl.RxPump(asm)
        self.assertEqual(pump.tick(), 0)


class HandshakeTest(unittest.TestCase):
    def test_hello_ack_grant_intersect(self):
        t = tl.BridgeTransport(epoch=5)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0102, roles=0xFF), handle=0x0102)
        acks = [f for f in t.out_frames if f[1] == tl.MSG_HS_HELLO_ACK]
        self.assertEqual(len(acks), 1)
        roles = acks[0][3][6]
        self.assertEqual(roles, tl.ROLE_CONTROL_CLIENT | tl.ROLE_SERVICE_CLIENT)
        self.assertEqual(tl.rd32(acks[0][3], 0), 5)  # epoch echoed
        self.assertEqual(acks[0][3][7], tl.PROFILE_NETWORK_BRIDGE)

    def test_two_principals_distinct_authorities(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0001), handle=0x0001)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0002), handle=0x0002)
        acks = [f for f in t.out_frames if f[1] == tl.MSG_HS_HELLO_ACK]
        a1 = tl.rd16(acks[0][3], 4)
        a2 = tl.rd16(acks[1][3], 4)
        self.assertNotEqual(a1, a2)

    def test_reject_payload_carries_code(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, bytes((tl.PROTO_MAJOR, tl.PROFILE_RADIO)) + tl.wr16(1) + b"\x01",
                   handle=1)
        rejects = [f for f in t.out_frames if f[1] == tl.MSG_HS_REJECT]
        self.assertEqual(len(rejects), 1)
        self.assertEqual(rejects[0][3][0], tl.REJECT_PROFILE_MISMATCH)

    def test_unknown_handshake_msg_rejected(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, 0x7F, tl.QUEUE_CONTROL, 0, b"", handle=None)
        self.assertEqual(t.stats.rejects.get(tl.REJECT_INVALID_ENVELOPE), 1)

    def test_short_hello_rejected(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, b"\x01", handle=None)
        self.assertEqual(t.stats.rejects.get(tl.REJECT_INVALID_ENVELOPE), 1)

    def test_no_reassign_within_epoch(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0007), handle=0x0007)
        # Same handle re-hello: same authority even though another handle
        # was allocated in between.
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0008), handle=0x0008)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0007), handle=0x0007)
        acks = [f for f in t.out_frames if f[1] == tl.MSG_HS_HELLO_ACK]
        auth = [tl.rd16(a[3], 4) for a in acks]
        self.assertEqual(auth[0], auth[2])
        self.assertNotEqual(auth[0], auth[1])


class EpochTest(unittest.TestCase):
    def test_epoch_clears_map(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0001), handle=0x0001)
        t.on_epoch_change(9)
        self.assertIsNone(t.resolve(0x0001))
        self.assertEqual(len(t.principals), 0)

    def test_authority_ids_restart_after_epoch(self):
        t = tl.BridgeTransport(epoch=1)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0001), handle=0x0001)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0002), handle=0x0002)
        t.on_epoch_change(2)
        t.on_frame(tl.FAMILY_HANDSHAKE, tl.MSG_HS_HELLO, tl.QUEUE_CONTROL,
                   tl.FLAG_RESERVE, hello_payload(0x0009), handle=0x0009)
        acks = [f for f in t.out_frames if f[1] == tl.MSG_HS_HELLO_ACK]
        self.assertEqual(tl.rd16(acks[-1][3], 4), 1)


class DemoVerdictsTest(unittest.TestCase):
    def test_all_demo_cases_pass(self):
        for name, ok, _stats in tl.demo_cases():
            self.assertTrue(ok, f"demo case failed: {name}")

    def test_cli_main_returns_zero(self):
        self.assertEqual(tl.main(), 0)


if __name__ == "__main__":
    unittest.main()
