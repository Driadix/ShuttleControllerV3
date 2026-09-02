# transport-proto: network_bridge link layer prototype (ticket #75)

Throwaway host model answering the #75 design questions BEFORE production
C++ lands. NOT production code: it never touches hardware and never enters
`platform/`, `domain/` or `adapters/`.

## Design questions answered

- **D1 Frame assembler**: byte stream -> canonical frames (0xE3 0x10, MTU
  128, CRC-16/CCITT-FALSE over header+payload). Resync = scan forward to
  the next sync pair; oversized `payload_len` in a header is garbage (never
  trust the wire length); partial frames wait; a 250 ms inter-frame gap
  discards a partial frame (V1 parser value, research C02). Counters:
  frames_ok / resyncs / dropped_bytes / bad_crc / truncated_flushes.
- **D2 RX budget**: per-tick pump moves a fixed share of the 230 B/tick
  link budget (#48 section 7) from a bounded driver ring into the
  assembler; ring overflow is DROPPED with a counter, never blocks.
- **D3 Handshake machine**: Hello -> HelloAck/HandshakeReject;
  protocolMajor hard-fail; expectedProfileId != effective -> ProfileMismatch;
  bridgePrincipalHandle is epoch-scoped, never reassigned in-epoch; grant =
  requested roles AND controller grant; 16-principal budget -> BusyRejected.
- **D4 Reconnect/gap**: partial frame + gap timeout -> truncated flush;
  epoch change clears the handle -> authority map; old principal must
  re-hello (HandshakeRequired until then).

## Run

```bash
.venv-pio312/Scripts/python.exe bench/transport-proto/transport_logic.py
.venv-pio312/Scripts/python.exe -m unittest discover -s bench/transport-proto/tests -t .
```

The CLI prints a machine-readable verdict document (PASS/FAIL + per-case
stats); unittest covers the same model plus frame-split/anywhere, CRC
reference vector, budget and epoch edge cases.
