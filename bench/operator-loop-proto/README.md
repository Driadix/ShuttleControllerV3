# PROTOTYPE - L4 operator loop (ticket #60)

Throwaway prototype answering the design question of
[issue #60](https://github.com/Driadix/ShuttleControllerV3/issues/60):
what should a reproducible L4 operator loop look like?

**This is a prototype, not production.** It validates the operator flow and
the scenario/result formats so ticket #65 can implement the production
verification runner from an approved contract. Do not reuse this code in
production paths.

## Question being answered

Does this operator flow feel right?

```
detect (board + port)
  -> owner safety-checklist (physical, per run)
  -> flash (ST-Link, when scenario requires)
  -> scenario run with timeout (UART capture)
  -> raw output artifact
  -> normalization to machine-readable result
  -> bind firmware SHA + board identity + toolchain identity
  -> verdict; REFUSE (INCOMPLETE) on missing evidence
```

The prototype exercises every step of the flow with cheap, throwaway code and
synthetic + refusal-path data. The real bench run (flash + live capture) is
performed only after the owner supplies the per-run physical checklist
attestation (see HITL briefing in ticket #60).

## Files

| File | Purpose |
| --- | --- |
| `proto_runner.py` | throwaway CLI: detect / checklist / run / normalize |
| `scenarios/*.json` | scenario format samples (schemaVersion 1) |
| `gen_fixture.py` | synthetic raw captures for off-bench pipeline demo |
| `fixtures/*.bin` | generated fixtures (bench-alive / silent / noisy) |

## Running (off-bench)

```bash
python gen_fixture.py

# normalize a raw capture -> machine-readable stats
python proto_runner.py normalize fixtures/bench-alive.bin

# full loop, simulated: PASS (bench-alive data, synthetic board identity)
python proto_runner.py run scenarios/uart-probe.json \
    --fixture fixtures/bench-alive.bin --simulate-board --out-dir out/demo-pass

# TIMEOUT: silent capture (kernel Phase-1 emits nothing on UART)
python proto_runner.py run scenarios/uart-probe.json \
    --fixture fixtures/silent.bin --simulate-board --out-dir out/demo-timeout

# INCOMPLETE refusal: flash scenario without signed checklist (no hardware touched)
python proto_runner.py run scenarios/flash-boot-smoke.json \
    --fixture fixtures/silent.bin --simulate-board --out-dir out/demo-refuse

# FAIL: noisy capture (bad CRC ratio above oracle threshold)
python proto_runner.py run scenarios/uart-probe.json \
    --fixture fixtures/noisy.bin --simulate-board --out-dir out/demo-fail
```

Exit codes: 0 PASS, 1 FAIL, 2 TIMEOUT, 3 INCOMPLETE (refusal).

## Simulation hooks vs production contract

`--fixture`, `--simulate-board` and `--yes` (auto-attestation) exist so the
pipeline mechanics can be demonstrated without a human at the bench or without
the bench at all. They are explicitly EXCLUDED from the production runner
contract (ticket #65): production evidence must come from a live ST-Link probe
and a live capture, and the checklist sign-off must be a genuine per-run owner
attestation (interactive or a pre-signed file) - never an automatic flag. The
refusal path is demonstrated for real: a missing board identity, missing
checklist, or missing SHA yields INCOMPLETE without touching hardware.

The committed `evidence/checklist.json` was signed by the owner (Driadix) in
the HITL session of 2026-08-13 (ticket #60 briefing); the `--yes` flag was the
recording mechanism, not the authorization.

## Live bench commands (owner checklist attestation required first)

```bash
# read-only board identity (ST-Link probe: idcode + UID)
python proto_runner.py detect --port COM9

# owner sign-off of the physical checklist
python proto_runner.py checklist --owner Driadix --out checklist.json

# full real loop
python proto_runner.py run scenarios/flash-boot-smoke.json \
    --port COM9 --checklist checklist.json --out-dir out/real-run
```

## Live run results (2026-08-13, owner attestation)

Representative flow executed on the bench (ControllerV6, ST-Link V2, COM9
relay display) after the owner's per-run physical attestation:

| Run | Result | Key evidence |
| --- | --- | --- |
| `flash-boot-smoke` | **PASS** (exit 0) | flash ok (6.4 s), checklist signed (Driadix), board STM32F405RG idcode `0x100f6413` UID `002900363033470336363131`, capture 15 s, evidence complete |
| `uart-probe` | **TIMEOUT** (exit 2) | real 20 s capture, 0 frames - Phase-1 kernel emits nothing on UART (decision: T16 observability after #72/#75), evidence complete |

Artifacts: `evidence/checklist.json`, `evidence/run-flash-boot-smoke/`,
`evidence/run-uart-probe/`. Raw captures are empty (0 B) - expected for the
Phase-1 kernel (KernelEvents stub is a no-op sink).

## Scenario format (schemaVersion 1)

```json
{
  "schemaVersion": 1,
  "id": "flash-boot-smoke",
  "title": "Flash verify: ST-Link flash + probe alive",
  "type": "flash-verify",
  "phase": "L4",
  "flash": { "required": true, "env": "firmware" },
  "capture": { "port": "auto", "baud": 230400, "parity": "E",
               "durationS": 15, "maxBytes": 2000000 },
  "oracle": { "minFrames": 0, "maxCrcBadRatio": 1.0,
              "requirePatterns": [], "forbidPatterns": [] }
}
```

`type`: `behavior` (boot behavior: requires `minFrames>=1` or non-empty
`requirePatterns`, else schema error) or `flash-verify` (PASS claims only
flash + probe alive, never boot behavior; `minFrames=0` allowed).
`minFrames` counts TOTAL received frames (valid + bad); a bad-only
stream reaching `minFrames` fails on `maxCrcBadRatio`, not TIMEOUT.

Verdict exit codes: PASS 0, FAIL 1, TIMEOUT 2, INCOMPLETE 3. Scenario
schema violations (vacuous behavior oracle, behavior claim in
flash-verify) are rejected BEFORE the run starts: exit 4, no side effects.

## Result format (schemaVersion 1)

`result-<scenario>.json` in the out-dir: schemaVersion, runner, scenario,
board {probe, part, idcode, uid}, uart {port, open, bytes}, firmware
{gitSha, gitDescribe, artifact, artifactSha256}, toolchain, checklist
{owner, signed, at, items}, flash {ok, env}, capture {rawPath, rawBytes},
normalized {bytes, framesValid, framesBad, msgCounts, logLines}, verdict,
reasons, evidence {complete, missing, resultPath}.

Verdict semantics: PASS = oracle satisfied and evidence complete;
TIMEOUT = oracle not satisfied within the window (evidence still complete);
FAIL = oracle violated; INCOMPLETE = evidence cannot be made complete
(refusal, non-zero exit).

## Prototype finding: refusal gates physical side effects

First run of the refusal path exposed an ordering flaw: the runner recorded
the missing-checklist refusal but still executed the ST-Link flash (the board
was actually re-flashed). Fixed: the flash step is now hard-gated on the
pre-flight evidence gates (board identity, uart port, checklist sign-off);
any missing gate skips the flash with `flash: {ok: false, skipped: true}`.
Production contract (ticket #65) MUST preserve this: a refusal is a hard stop
before any physical operation, never a post-hoc verdict on top of it.

Note: the first (buggy) run uploaded the production kernel from #70
(`080c114`, artifact sha256 `959e234e...`) to the bench board before the fix;
the board now runs that kernel (Phase-1, silent UART - expected). Restoring
the bring-up firmware is the owner's call (source lives in the V1 repo).

## Exit codes

0 PASS, 1 FAIL, 2 TIMEOUT, 3 INCOMPLETE (refusal).
