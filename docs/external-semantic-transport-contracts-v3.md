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
- **effective profile** определяется контроллером по **ingress adapter / provisioned endpoint**, не по client field;
- **mutating Update-class допускается только когда effective profile = `network_bridge`**.


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
| `authorityId` | u16 | controller-assigned at handshake; payload echo only — never sole resolver of principal |
| `bridgePrincipalHandle` | u16 | bridge-asserted on **every** principal-scoped frame on effective `network_bridge`; maps to authorityId |
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
- `effectiveProfileId` (controller-derived from ingress; authoritative);
- `supportedCapabilities` (bitmask/set);
- profile-relevant limit flags (без обязательных чисел budgets — они в #48, могут advertise как opaque limit class later);
- assigned `authorityId` + **granted** role set / capability grant for this principal (controller-authored).


**Client → controller (hello / interest):**

- `expectedProfileId` (`network_bridge` | `radio`) — **неавторитетное** ожидание клиента для fail-fast mismatch; **не** выбирает effective profile;
- optional `endpointInstanceHint` (non-authoritative label for logs; **не** identity proof и **не** multi-principal handle);
- на effective `network_bridge` (см. ниже): **`bridgePrincipalHandle`** (u16) — bridge-asserted principal selector;
- **requested** roles interest / `clientCapabilities` (заявки, не права).

**Effective profile (anti-spoof):**

- Контроллер выводит `effectiveProfileId` **только** из ingress path: какой transport adapter / UART / provisioned endpoint принял кадр (radio adapter vs network_bridge adapter), плюс provisioning binding этого endpoint.
- Client **не** может повысить привилегии, отправив `expectedProfileId=network_bridge` на radio ingress.
- Если client прислал `expectedProfileId` и оно ≠ `effectiveProfileId` → handshake/request reject `ProfileMismatch` (стабильный код).
- Controller advertise в handshake/ACK включает `effectiveProfileId`, который client обязан учитывать.
- Все hard profile rules (Update-class, budgets, framing expectations) применяются к **effective** profile.

**Назначение principal / `authorityId` (channel-trust, без crypto session на wire контроллера):**

- `authorityId` **всегда назначает контроллер**. Клиент конечного UI **не** self-binds identity.
- **Resolver principal (норматив):** контроллер **никогда** не выбирает principal только по payload `authorityId`.  
  - effective `radio` → principal = map(radio ingress/endpoint);  
  - effective `network_bridge` → principal = map(bridgeEndpointId, bridgePrincipalHandle) на **каждом** principal-scoped кадре.

- **effective `radio`:** ровно **один** principal на radio ingress/endpoint. Поле `bridgePrincipalHandle` **запрещено** (присутствует → `InvalidEnvelope` / `Unauthorized`).
- **effective `network_bridge` — multi-principal через bridge-asserted handle:**
  1. Физически у контроллера один bridge ingress; multiplex downstream TCP/UI клиентов делает **provisioned bridge** (доверенный peer channel-trust), не контроллер.
  2. Bridge **обязан** на hello и на **каждом** последующем principal-scoped кадре (mutating Control/Service/Update/Session, handshake refresh) ставить **`bridgePrincipalHandle`** (u16) в transport/header envelope (не «только payload по желанию клиента»).
  3. Handle opaque; bridge ассоциирует 1:1 с одним своим authenticated/authorized downstream client. Поле авторитетно **только** потому что кадр пришёл на effective `network_bridge` ingress от provisioned bridge. **Не** credential конечного пользователя; **не** действительно на radio.
  4. Hello: controller map `(bridgeEndpointId, bridgePrincipalHandle) → authorityId` (+ grant). Первый hello handle в epoch → allocate `authorityId` если budget не исчерпан; иначе reject. Повтор hello с тем же handle в **том же** `controllerEpoch` → **тот же** `authorityId` и тот же principal (re-bind/refresh grant allowed; ledger identity unchanged).
  5. **Handle lifetime / no-reassign (epoch-scoped):**
     - В пределах одного `controllerEpoch` значение `bridgePrincipalHandle` на данном `bridgeEndpointId` является **стабильным ключом principal** и **не может быть переназначено** другому downstream-клиенту/principal.
     - Disconnect downstream **не** освобождает handle для reuse другим клиентом до смены epoch. Bridge MAY keep using the same handle only for the **same** logical downstream client on reconnect; a **different** client MUST receive a handle value **never used** on this bridge endpoint in the current epoch.
     - On `controllerEpoch` change controller **clears** handle→authorityId map and ledgers; handles may be issued fresh.
     - Exhaustion of unused handle/authority budget in epoch → reject `BusyRejected` (numbers → #48).
  6. **Post-handshake invariant (anti cross-principal spoof):**
     - Controller resolves `resolvedAuthorityId = map(bridgeEndpoint, handle)` from the frame handle.
     - Unknown/unmapped handle → reject `Unauthorized` / `HandshakeRequired`.
     - Payload may **echo** `authorityId` for client correlation; if present and `≠ resolvedAuthorityId` → reject `Unauthorized` (client A cannot act as client B by stuffing B's id).
     - Controller admission, idempotency ledger, grants use **resolvedAuthorityId only**.
  7. **Bridge obligation:** for each downstream connection the bridge MUST (a) allocate a handle obeying no-reassign-in-epoch, (b) fix that handle for the connection after its auth, (c) stamp it on every principal-scoped frame, (d) **rewrite or drop** any client-supplied attempt to present another connection's `authorityId`/handle, (e) on disconnect either reconnect same client with same handle or assign a fresh never-used-in-epoch handle to a new client — never silently repoint an in-epoch handle at a different client. Failure of (c)/(d)/(e) is a bridge defect; controller still enforces (5)/(6).
  8. Смена handle = смена principal (нужен handshake/grant для нового handle). `endpointInstanceHint` **никогда** не участвует в map.

- Заявленные role/capability fields — только **requests**. Grant ⊆ allowed(principal) ∩ requested ∩ controllerSupports. Вне grant → `Unauthorized` / `RoleEscalation`.
- Отсутствие crypto session на wire **не** означает self-asserted authority.

**Правила:**

- major mismatch → hard fail handshake;
- unknown **optional** capability в request → ignore;
- required capability missing у controller → reject with stable code;
- capabilities **не** заменяют authority admission и **не** расширяют grant;
- N concurrent `authorityId` на effective `network_bridge` через различные handles; exclusive control slot один (#46);
- idempotency ledger keyed per **resolved** `authorityId`;
- role в payload должна быть ∈ grant **resolved** principal;
- radio path с `bridgePrincipalHandle`, или bridge principal-scoped frame без handle → reject.





### 5.2 Evolution

- **Major:** breaking wire/semantics; old clients must not mutate.
- **Minor/patch:** additive messages/fields behind capabilities; contract tests per version.
- Layout freestyle changes запрещены.

## 6. Security boundaries (Q5)

Threat model core v3: **channel-trust by deployment** (локальный bridge UART/TCP в site trust, own radio link). Операционное доверие к deployment-каналу **не** равно self-issued privileges на wire.

| Mechanism | Protects | Does **not** provide |
|---|---|---|
| `frameChecksum` | accidental corruption | authenticity vs active peer, anti-spoof |
| Controller-assigned `authorityId` + **granted** roles + admission | operation class / unprovisioned surface; claimed-role escalation | cryptographic proof of human operator; hostile-link anti-spoof beyond channel trust |
| Capability gates + **effective** profile hard rules | Update-class only on effective network_bridge; feature surface; profile spoof rejected | rights beyond grant / claimed profileId |
| **`bridgePrincipalHandle` on effective network_bridge only** | distinct principals behind one bridge UART under provisioned-bridge trust | end-user auth; not valid on radio; not replaceable by endpointInstanceHint |
| OTA image checksum + signature fields | authenticity/integrity **of image bytes** | authorization to begin/abort/pause update transaction (Update Authority + admission + grant) |
| Transport MAC/TLS | — | **not in core v3**; future capability/profile extension |
| Bridge-side client auth (outside controller wire) | which downstream client the bridge maps to a handle | end-to-end crypto to controller |


Explicit non-claims: no hostile-RF confidentiality, no mutual auth crypto session on controller wire in core.


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

- `requestId`, `controllerEpoch`, `authority` role (**must be ∈ handshake grant**), `authorityId` (**controller-assigned; echo only**), `operationType`, `parameters` (typed; empty object allowed), conditional `parentOperationId` (not used by external clients for child creation under normal rules).


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
- `RoleEscalation` (claimed role/capability outside controller grant)
- `UnsupportedVersion`
- `UnknownCapabilityRequired`
- `InvalidEnvelope`
- `InvalidParameters`
- `Conflict` (idempotency key payload mismatch)
- `ResourceConflict`
- `WrongWindow`
- `HealthGate`
- `ProvisioningGate`
- `ProfileDenied` (e.g. Update on effective radio)
- `ProfileMismatch` (client `expectedProfileId` ≠ controller `effectiveProfileId`)
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
7. Update mutating denied on **effective** radio (`ProfileDenied`) even if payload claims otherwise.  
8. Session stale `sessionSeq` does not refresh lease.  
9. `SetWallClock` does not move monotonic timers (property test).  
10. Multi-principal on bridge: two different `bridgePrincipalHandle` values on effective network_bridge get distinct `authorityId` and separate idempotency ledgers; second exclusive activity → `ResourceConflict`.  
11. No hidden semantics checklist (G3 item): external client with registries+schema alone can decode admissions, outcomes, faults.  
12. Authority binding: principal resolved from ingress (+ handle on bridge), never from client-chosen `authorityId` alone; payload id is echo; mismatch with resolved → `Unauthorized`.  
13. **Impersonation / claimed-role escalation:** request with role or capability outside handshake grant → `RoleEscalation`/`Unauthorized`; endpoint/profile alone never implies Update Authority without grant.  
14. **Bridge principal handle:** (a) two handles → two authorityIds; (b) same handle re-hello → same authorityId in epoch; (c) `endpointInstanceHint` alone never splits principals; (d) handle on radio → reject; (e) missing handle on bridge hello or principal-scoped frame → reject; (f) **cross-principal spoof:** handle=A + payload authorityId=B → reject; (g) **handle lifetime:** after H→authorityA, disconnect, reconnect **same** client with H → still authorityA; (h) **no reassign:** bridge must not map H to a different client in-epoch — controller continues treating H as authorityA for entire epoch (burned handle); new client requires unused handle; epoch change clears map.  
15. **Profile spoofing:** frame on radio ingress with `expectedProfileId=network_bridge` → `ProfileMismatch` / no network_bridge privileges; UpdateBegin on radio ingress → `ProfileDenied` regardless of claimed expected profile.



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
| Q3/Q6 | Profiles: `network_bridge` + `radio` only (no `display`); effective profile from ingress |
| Q4 | Mandatory handshake before mutating |
| Q5 | Channel-trust + role authz; checksum ≠ authenticity; OTA sig ≠ transaction authz |
| Q7 | Hard link/budgets; soft interest via capabilities |
| Q8 | Mutating Update only on effective `network_bridge` |
| Q9 | Dual-plane correlation |
| Q10 | Bidirectional handshake; expectedProfileId non-authoritative; controller assigns authorityId + grant |
| Q11 | Families by exchange class |
| Q12 | Explicit queueClass + #43 policies |
| Q13 | Session-scoped lease stream |
| Q14 | SetWallClock; monotonic untouched |
| Q15 | Transactional update messages (schema here, mechanics #50) |
| Q16 | Layered stable registries |
| Q17 | Per-link FIFO per class |
| Q18 | Split transport vs semantic timers |
| Q19 | Controller-assigned authorityId; requests echo grant role+id; no self-binding identity |
| Q20 | protocolVersion + capabilities evolution |
| Q21 | #47 schemas; #50 data model/flows |
| Q22 | Query + bounded sub + optional streams |
| Q23 | Multi-principal via bridgePrincipalHandle on effective network_bridge only; one exclusive slot; radio = single principal |
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
