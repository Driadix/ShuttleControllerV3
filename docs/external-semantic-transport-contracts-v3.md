# External Semantic & Transport Contracts V3

Статус: **утверждено владельцем (Q1–Q26)**; резолюция — тикет [«Специфицировать protocol V3 (wire, transport profiles, negotiation, security)»](https://github.com/Driadix/ShuttleControllerV3/issues/47) карты #1.  
Logical item 6 нормативного пакета (issue 8), gate G3.  
Входы: semantic contract ([#13](https://github.com/Driadix/ShuttleControllerV3/issues/13), `docs/semantic-contract-v3.md`), system context ([#2](https://github.com/Driadix/ShuttleControllerV3/issues/2)), architecture/queues/Time ([#43](https://github.com/Driadix/ShuttleControllerV3/issues/43), `docs/software-architecture-boundaries-v3.md`), lifecycle ([#46](https://github.com/Driadix/ShuttleControllerV3/issues/46), `docs/lifecycle-axes-v3.md`), safety domain conditions ([#45](https://github.com/Driadix/ShuttleControllerV3/issues/45); wire registry здесь).  
Термины — `CONTEXT.md`. Совместимость wire с V1/V7 **не требуется**. Реализация внешних клиентов и адаптеров (UI дисплея, HTTP/JSON, WMS) **вне scope** этой карты.

## 1. Назначение и границы

Документ задаёт, как единый semantic contract контроллера кодируется на wire и проектируется на транспортные профили:

- shared **contract core** (смысл сообщений, envelope, errors, versioning);
- **transport profiles** (проекции без смены смысла);
- handshake, correlation, queues, session, time-set, update transport surface, security boundaries;
- verification obligations уровня контракта.

Не задаёт:

- численные budgets/таймауты/MTU (item 9 / [#48](https://github.com/Driadix/ShuttleControllerV3/issues/48));
- catalog параметров конфигурации, slot/bootloader/OTA mechanics, RTC power ([#50](https://github.com/Driadix/ShuttleControllerV3/issues/50));
- engineering baseline/codegen toolchain ([#51](https://github.com/Driadix/ShuttleControllerV3/issues/51));
- полный binary layout каждого payload field bit-exact beyond identity widths и structural rules (дополняется schema tables + contract tests при наполнении item 6).

Инвариант: **транспорт кодирует контракт и не изменяет его смысл**. ACK/reject/accept решает Semantic Contract & Admission; transport adapter только фреймит, парсит в бюджете и эмитит.

## 2. Слойность (Q1, Q2)

```
┌─────────────────────────────────────────────┐
│  Semantic Contract & Admission (domain)     │
│  request/operation/session/update policies  │
└──────────────────▲──────────────────────────┘
                   │ encode/decode only
┌──────────────────┴──────────────────────────┐
│  Contract core (this doc)                   │
│  binary framing + typed messages + registries│
└──────────────────▲──────────────────────────┘
          ┌────────┴────────┐
          │                 │
   network_bridge         radio
   transport profile      transport profile
```

- **Contract core:** canonical binary framing + typed payloads (не CBOR/protobuf как обязательный core).
- **Profile:** physical/framing wrapper, MTU/byte-budget defaults, link-loss hooks, default interest hints; **не** отдельный dialect смысла.
- JSON/gRPC/HTTP и UI — преобразования **на network bridge**, не на контроллере.

## 3. Transport profiles (Q3/Q6/Q7/Q8)

### 3.1 Inventory

| Profile ID | Физический baseline (V1 evidence) | Роль |
|---|---|---|
| `network_bridge` | UART path, ранее «display» (230400 8E1-класс) | Доверенный мост: TCP debug, будущий HTTP/JSON и др. UI/дисплей **за** мостом |
| `radio` | E22 UART + route header | Доверенный radio control/diagnostics client |

Отдельного profile `display` **нет**: дисплей не является peer semantic contract контроллера.

### 3.2 Hard vs soft differences

**Hard (норма profile):**

- framing wrapper (например E22 `{addh,addl,channel}` перед core frame на radio; raw core frame на network_bridge UART);
- MTU / bytes-per-tick / depth budgets (числа — #48);
- link detection / RX frame timeout hooks;
- **mutating Update-class допускается только на `network_bridge`**.

**Soft (capabilities ∩ authority, не hard allowlist message family):**

- какие optional queries/subscriptions/streams активны;
- client-chosen cadence/interest (trusted clients);
- radio «просит меньше» — usage client, не запрет protocol core.

**Общие hard gates (не «магия порта»):** authority/role admission, unprovisioned surface, exclusive slot, platform window, health/provisioning guards, queue overload reject.

### 3.3 Extension

Новый profile = новая проекция тех же core messages + hard link traits. Новый semantic dialect запрещён без impact review item 6.

## 4. Encoding core frame (Q2, Q9, Q24)

### 4.1 Frame layout (normative structure)

```
| sync0 | sync1 | header | payload | frameChecksum |
```

- `sync` — фиксированные байты versioned framing (конкретные значения в schema table; **не** обязаны совпадать с V1 `0xBB 0xCC`).
- `header` (минимум): `protocolMajor`, `msgFamily`, `msgType`, `queueClass`, `flags`, `frameSeq`, `payloadLen`.
- `payload` — little-endian typed record по `(msgFamily, msgType, protocolVersion)`.
- `frameChecksum` — integrity против **случайной** порчи (CRC-класс). **Не** является security authenticity/MAC.

### 4.2 Fixed identity widths

| Field | Width | Notes |
|---|---|---|
| `controllerEpoch` | u32 | opaque boot-instance; controller-authored |
| `requestId` | u32 | unique per `(controllerEpoch, authorityId)` logical request |
| `operationId` | u32 | controller-authored after accept; never client-supplied as identity |
| `authorityId` | u16 | principal bound at handshake |
| `sessionId` | u16 | manual session instance |
| `sessionSeq` | u16 | monotonic per session |
| `frameSeq` | u8 или u16 | per-link transport; profile table |

Strings/UUID на hot path **не** используются.

### 4.3 Dual-plane correlation (Q9)

| Plane | Fields | Purpose |
|---|---|---|
| Transport | `frameSeq`, transport ACK/`refFrameSeq` | loss/dup на link, parser pacing |
| Semantic | `controllerEpoch`, `requestId`, `authorityId`, `operationId` | admission, idempotency, outcomes |

`frameSeq` **не** заменяет `requestId`. Retry того же logical request сохраняет `requestId` (#13).

## 5. Handshake, versioning, capabilities (Q4, Q10, Q20, Q23)

### 5.1 Mandatory handshake

Mutating traffic (Control/Service/Update, session open, config set, time-set, stop intents that mutate) **запрещён** до успешного handshake на данном link/principal и после смены `controllerEpoch` — до refresh.

**Controller → client (advertise):**

- `protocolVersion` (major.minor.patch wire encoding);
- `controllerEpoch`;
- `firmwareIdentity` (type/build id opaque);
- `supportedCapabilities` (bitmask/set);
- profile-relevant limit flags (без обязательных чисел budgets — они в #48, могут advertise как opaque limit class later).

**Client → controller (bind):**

- `profileId` (`network_bridge` | `radio`);
- claimed roles interest / `clientCapabilities`;
- `clientIdentity` binding material → выдаваемый/подтверждаемый `authorityId` (не shared-secret crypto session).

**Правила:**

- major mismatch → hard fail handshake;
- unknown **optional** capability → ignore;
- required capability missing → reject with stable code;
- capabilities **не** заменяют authority admission;
- N principals concurrent на `network_bridge` допустимы; exclusive control slot всё равно один (#46);
- idempotency ledger keyed per `authorityId`.

### 5.2 Evolution

- **Major:** breaking wire/semantics; old clients must not mutate.
- **Minor/patch:** additive messages/fields behind capabilities; contract tests per version.
- Layout freestyle changes запрещены.

## 6. Security boundaries (Q5)

Threat model core v3: **channel-trust by deployment** (локальный bridge UART/TCP в site trust, own radio link). Clients считаются операционно доверенными, но **не** снимают admission.

| Mechanism | Protects | Does **not** provide |
|---|---|---|
| `frameChecksum` | accidental corruption | authenticity vs active peer, anti-spoof |
| Authority role + `authorityId` + admission | unauthorized operation class / unprovisioned surface | confidential channel |
| Capability gates + profile hard rules | Update-class only on network_bridge; feature surface | proof of human operator |
| OTA image checksum + signature fields | authenticity/integrity **of image bytes** | authorization to begin/abort/pause update transaction (Update Authority + admission) |
| Transport MAC/TLS | — | **not in core v3**; future capability/profile extension |

Explicit non-claims: no hostile-RF confidentiality, no mutual auth crypto session in core.

## 7. Message taxonomy (Q11)

| Family | Direction (typ.) | Queue class | Examples |
|---|---|---|---|
| `Handshake` | bi | Control (setup) | Hello, HelloAck, EpochAnnounce, CapabilityBind |
| `Control` | in/out | Control | OperationRequest, AdmissionAck, StopIntent, Query, Subscribe |
| `Service` | in/out | Service | SetWallClock, ConfigGet/Set/Sync, provisioning ops, diagnostics cmds |
| `Update` | in/out | Update | Begin, StageChunk, Finalize, Abort, UpdateStatus (**network_bridge only** mutating) |
| `Session` | in/out | Control | SessionOpen/Close, HoldToRunIntent, SessionReject |
| `Observability` | out (+ sub mgmt in) | telemetry / events / logs / traces | Snapshot fragments, Event, LogRecord, TraceRecord |
| `Outcome` | out | events (primary) | OperationTerminal, Progressless status already in snapshot |

Family boundaries существуют для policy/tests; плоский V1 MsgID enum **не** переносится как норма.

## 8. Queue projection (Q12, #43)

### 8.1 Inbound

| Class | Content | Overload |
|---|---|---|
| Control | ops, queries, subs, session, handshake | reject new with explicit code when full |
| Service | config/time/calib/diag mutating+ro service | reject when full |
| Update | update transaction messages | reserved capacity while in-progress; reject **new** transactions when saturated |

### 8.2 Outbound

| Class | Overload |
|---|---|
| telemetry | drop-oldest (freshness) |
| events | reserved capacity; drop-newest on overflow |
| logs | reserved capacity; drop-newest |
| traces | bounded; drop policy as traces class (prefer drop-oldest for volume streams; reserved for fault-correlated traces if flagged) |

Каждый drop/reject → counter (Observability Producer) + event.  
`queueClass` присутствует на wire (explicit). Profile задаёт budgets, не другую policy.  
Force-stop на CAN **вне** этих очередей и **вне** client protocol.

## 9. Operation request / ACK encoding (Q9, Q16, #13)

Mutating operation request payload minimum:

- `requestId`, `controllerEpoch`, `authority` role, `authorityId`, `operationType`, `parameters` (typed; empty object allowed), conditional `parentOperationId` (not used by external clients for child creation under normal rules).

Positive admission ACK minimum:

- `requestId`, `controllerEpoch`, `operationId`, `operationType`, `parentOperationId` null for root, result=`Accepted`.

Negative ACK minimum:

- `requestId`, `controllerEpoch`, `AdmissionRejectionCode`, **no** `operationId`.

Stop/cancel: control intent referencing `operationId`; idempotent; not a new operation type (Q25).

## 10. Manual session wire (Q13, #46)

- Open/close — Control admission; **`operationId` не выдаётся**.
- Active: `sessionId` + monotonic `sessionSeq` per hold-to-run intent.
- Stale/duplicate `sessionSeq` → reject; does not refresh lease.
- Fresh valid intent refreshes lease (lease counters owned by Manual Session domain).
- Transport ACK ≠ lease grant.
- Expiry / fault → Closing + controlled stop (INV-LEASE-STOP).
- Recovery-jog = same session messages with policy flag bound at open (immutable for session lifetime).

## 11. Time-set (Q14, #43)

- Service message `SetWallClock`: wall time fields + source mark (NTP/backend).
- Writes **RTC / wall clock only**.
- **Monotonic clock never adjusted** by this message.
- Domain timeouts (lease, freshness, no-progress) use monotonic exclusively.
- Optional readback: current wall + quality/source flags.
- Does not change `controllerEpoch`.
- Authority: Service Client (or explicitly permitted role); else reject.

## 12. Update transport surface (Q15, Q8; mechanics #50)

Mutating only on `network_bridge` with Update Authority + admission (window Serving→Update, stationary, slot, health Ready/Degraded).

Normative message set:

| Message | Purpose |
|---|---|
| `UpdateBegin` | start transaction; image meta (size, checksum alg, signature alg, version ids) |
| `UpdateStageChunk` | ordered chunk payload + offset + chunk checksum |
| `UpdateFinalize` | end staging; request validation |
| `UpdateAbort` | abort staging → lifecycle `Aborted` |
| `UpdateStatus` | query/event: stage, bytes, error, axis state |

Rules:

- maps to Update queue class + lifecycle axis Staging/Applying/… (#46);
- image **checksum + signature** fields mandatory at finalize/begin as schema;
- signature verifies **image**, not the right to run the transaction;
- Applying/boot/slots/bootloader/W_apply — #50 / #48 / proving slice;
- radio may receive **read-only** update status if capability present; cannot mutate.

## 13. Config / provisioning surface split (Q21)

**In #47 (this doc):**

- Service message schemas: get/set/sync/provisioning operation requests;
- live vs persist intent flags on wire;
- validation/rejection codes.

**In #50:**

- parameter catalog, persistence journal, commissioning flows, identity assignment, RTC power, bootloader.

## 14. Observability delivery (Q22, #2, #43)

- **Query snapshot** — authoritative reconciliation model (includes `controllerEpoch`).
- **Subscriptions** — bounded agreements; parameters limited by caps + budgets.
- **Streams** — telemetry/events/logs/traces as four outbound classes.
- No mandatory continuous push without subscription or explicit profile default capability.
- Events do not replace query after delivery loss.
- Fault/warning bits in telemetry must use registry codes (below), not ad-hoc strings as sole signal.

## 15. Ordering and timers (Q17, Q18)

### Ordering

- Per-link, per inbound class: FIFO processing.
- No global total order across links, principals, or classes.
- Outbound classes independent.
- Semantic idempotency covers dup/retry.

### Timers

| Layer | Examples | Effect |
|---|---|---|
| Transport | RX frame timeout, ACK wait, link-quiet | drop partial frame / declare link degraded / transport error codes |
| Semantic | lease, type link-loss policy, epoch | session stop; op continue/controlled_stop/fail_safe per type; EpochMismatch |

Link-loss **не** означает автоматическую отмену всех операций. Числовые значения — #48.

## 16. Layered registries (Q16, #45)

### 16.1 TransportResult

Frame/parse/budget/link errors (checksum fail, bad length, overload drop notification, handshake required, profile denied).

### 16.2 AdmissionRejectionCode (stable)

Minimum set (extensible):

- `EpochMismatch`
- `HandshakeRequired`
- `Unauthorized`
- `UnsupportedVersion`
- `UnknownCapabilityRequired`
- `InvalidEnvelope`
- `InvalidParameters`
- `Conflict` (idempotency key payload mismatch)
- `ResourceConflict`
- `WrongWindow`
- `HealthGate`
- `ProvisioningGate`
- `ProfileDenied` (e.g. Update on radio)
- `SequenceStale`
- `BusyRejected` (class full)  
  (plus type-precondition codes as catalog evolves)

### 16.3 OperationOutcomeCode

Terminal typed codes per operation type docs + common `Cancelled`/`Failed` families; diagnostic context optional bounded.

### 16.4 Fault / Warning wire registry

- Domain detection conditions live in safety model (#45).
- **Wire codes + latch/timed attributes** owned here (item 6).
- Clients must not need hidden firmware strings to interpret safety-relevant state.
- Initial mapping from V1 bitmasks is evidence for disposition preserve/change/exclude; V1 codes not frozen by wire-compat.

## 17. Stop and safety visibility (Q25)

- Client stop/cancel → control intent on `operationId` or session close.
- Safety Authority internal force-stop on CAN is **not** a client protocol frame.
- Evacuate and other safety-originated roots may be visible as operations/outcomes when externalized by domain rules.
- Fault latch/recovery acknowledgement follows safety/lifecycle docs; protocol carries codes + query fields only.

## 18. Verification obligations (contract-level)

1. Schema/contract tests: frame parse, checksum fail, payload version matrix.  
2. Handshake required before mutating; post-reboot epoch refresh.  
3. Capability intersection and major mismatch.  
4. Dual-plane correlation: retry same `requestId`; `frameSeq` reuse does not create new logical request.  
5. Negative ACK never carries `operationId`.  
6. Queue class overload: reject vs drop-oldest vs drop-newest behaviors + counters.  
7. Update mutating denied on radio (`ProfileDenied`).  
8. Session stale `sessionSeq` does not refresh lease.  
9. `SetWallClock` does not move monotonic timers (property test).  
10. Multi-principal: two authorities query ok; second exclusive activity → `ResourceConflict`.  
11. No hidden semantics checklist (G3 item): external client with registries+schema alone can decode admissions, outcomes, faults.  
12. Authority mismatch endpoint≠role tests.

Численные stress bounds — #48; HIL — verification pyramid #52.

## 19. Deferred / handoffs

| Topic | Owner |
|---|---|
| Numeric budgets, timeouts, MTU, cadences | #48 |
| Bootloader, slots, activation, RTC power, param catalog | #50 |
| Toolchain/codegen if ever introduced | #51 |
| Bit-exact payload tables for every operationType parameter | item 6 elaboration + per-op contracts |
| Proving non-blocking TX / radio AUX | #54 / architecture obligations |

## 20. Decisions index (grilling)

| ID | Decision |
|---|---|
| Q1 | Shared contract core + transport profiles |
| Q2 | Canonical binary framing + typed payloads |
| Q3/Q6 | Profiles: `network_bridge` + `radio` only (no `display`) |
| Q4 | Mandatory handshake before mutating |
| Q5 | Channel-trust + role authz; checksum ≠ authenticity; OTA sig ≠ transaction authz |
| Q7 | Hard link/budgets; soft interest via capabilities |
| Q8 | Mutating Update only on `network_bridge` |
| Q9 | Dual-plane correlation |
| Q10 | Bidirectional handshake + client capabilities |
| Q11 | Families by exchange class |
| Q12 | Explicit queueClass + #43 policies |
| Q13 | Session-scoped lease stream |
| Q14 | SetWallClock; monotonic untouched |
| Q15 | Transactional update messages (schema here, mechanics #50) |
| Q16 | Layered stable registries |
| Q17 | Per-link FIFO per class |
| Q18 | Split transport vs semantic timers |
| Q19 | Principal from handshake; role+authorityId on requests |
| Q20 | protocolVersion + capabilities evolution |
| Q21 | #47 schemas; #50 data model/flows |
| Q22 | Query + bounded sub + optional streams |
| Q23 | Multi-principal; one exclusive slot |
| Q24 | Fixed binary identity widths |
| Q25 | Stop as control intent; no client force-stop bus |
| Q26 | This normative document |

## 21. Ссылки

- `docs/semantic-contract-v3.md`
- `docs/software-architecture-boundaries-v3.md`
- `docs/lifecycle-axes-v3.md`
- `docs/safety-model-v3.md`
- `CONTEXT.md`
- V1 evidence: `Driadix/ShuttleController` `Cntrl_V2/ShuttleProtocol.h` @ production main (baseline only)
