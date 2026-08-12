# Baseline Manifest V3 (gate G6)

Статус: **Approved - profile qualification rebaseline items 2/6/7/8/11/12/13; финальная ревизия `7491869` (status finalization, PR #79), семантическое evidence `9b431a7` (PR #78); тикет [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59) закрывается после merge настоящего manifest PR**. Исторический gate G6 закрыт тикетом [#56](https://github.com/Driadix/ShuttleControllerV3/issues/56) для предыдущих revisions. Класс: Derived View (issue 8 §2: manifest и traceability/status/coverage матрицы; проверяются на полноту и консистентность, не получают повторного смыслового approval). Единая точка входа нормативного пакета V3.

Правила: изменение любого включённого item revision - по change control (#8 §9, классы Editoral/Clarifying/Semantic/Baseline-affecting); manifest сам по себе не дублирует нормативное содержание (только ревизии, статусы, gates, approval records).

## 1. Item manifest

| # | Logical item | Документ-носитель | Ревизия (commit, main) | Тикет | Gate | Статус | Approval |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Project Assurance & Governance | issue 8 resolution + `docs/agents/*.md` (tracker/triage/hitl/domain conventions) | issue 8 (immutable resolution) | #8, #7 | G0 | Approved | владелец |
| 2 | System Context & Concepts | `CONTEXT.md` + issue 2 resolution | `9b431a7` (семантика; status-изменений нет) | #2, #59 | G0 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78) |
| 3 | Stakeholder & System Requirements | распределены: `docs/semantic-contract-v3.md`, `docs/quality-attributes-and-budgets-v3.md`, per-operation docs | см. items 4/6/9 | #2, #9, #13 | G1 | Approved (распределённый) | владелец |
| 4 | Capability & Operation Contracts | `docs/semantic-contract-v3.md` + 14 algorithmic docs (#16, #30-42) | `1cd2f1b`; algorithmic docs `7873943` (группа) | #13, #16, #30-42, #9, #14 | G1 | Approved | владелец |
| 5 | Safety & Health | `docs/safety-model-v3.md` | `1b08bb8` | #45 | G2 | Approved | владелец |
| 6 | External Semantic & Transport Contracts | `docs/external-semantic-transport-contracts-v3.md` | `7491869` | #47, #59 | G3 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78), статус `7491869` (PR #79) |
| 7 | Configuration, Identity & Lifecycle | `docs/configuration-identity-lifecycle-v3.md` + `docs/lifecycle-axes-v3.md` | `7491869` | #50, #46, #59 | G2/G3 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78), статус `7491869` (PR #79) |
| 8 | Observability & Diagnostics | `docs/observability-architecture-v3.md` | `7491869` | #49, #59 | G3 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78), статус `7491869` (PR #79) |
| 9 | Quality Attributes & Budgets | `docs/quality-attributes-and-budgets-v3.md` | `1b08bb8` | #48 | G4 | Approved | владелец |
| 10 | Software Architecture | `docs/software-architecture-boundaries-v3.md` + issue 10 (execution) + `docs/proving-slice-v3.md` | `1b08bb8`; issue 10 resolution; `9c045f1` | #43, #10, #54 | G4 | Approved (execution conditional, host-only evidence) | владелец |
| 11 | Engineering & Release | `docs/engineering-and-release-baseline-v3.md` | `7491869` | #51, #59 | G5 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78), статус `7491869` (PR #79) |
| 12 | Verification Strategy & Acceptance | `docs/verification-strategy-v3.md` | `7491869` | #52, #59 | G5 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78), статус `7491869` (PR #79) |
| 13 | Implementation Plan | `docs/implementation-plan-v3.md` | `7491869` | #57, #59 | G6 | Approved (profile qualification rebaseline) | независимый review + владелец, семантика `9b431a7` (PR #78), статус `7491869` (PR #79) |

Evidence assets (класс Evidence, #8 §2): `docs/research/v1-system-evidence-index.md` (тег `evidence/v1-system-evidence-index`), `docs/research/v1-execution-evidence.md` (`evidence/v1-execution-evidence`), `docs/research/v3-capability-evidence-slices.md` (`evidence/v3-capability-evidence-slices`), `docs/research/proving-slice-report.md` (host-only, #54), `docs/proving-slice-v3.md` (design, #54).

Profile qualification rebaseline [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59): единый binary сохраняет supported profiles 800/1000/1200; production qualification принадлежит конкретному signed app image; `v1.0.0` получает singleton qualification target на gate Фазы 3; glossary/wire/lifecycle/observability/release/verification/implementation items изменены согласованно. Утверждено независимым embedded/RT review (APPROVE, 0 findings) и владельцем: семантика `9b431a7` (PR #78, merge `6bf7b14`), статус `7491869` (PR #79, merge `80b0759`).

## 2. Status matrix (по классам #8 §2)

| Класс | Покрытие | Статус |
| --- | --- | --- |
| Normative (13 items) | items 2, 6, 7, 8, 11, 12 и 13 утверждены profile qualification rebaseline | Approved (13/13): семантика `9b431a7` (PR #78), статус `7491869` (PR #79); независимый review + владелец; #59 закрывается после merge manifest PR |
| Evidence | V1-индексы, capability slices, proving slice отчёт | verified (independent review) |
| Governance | issue 8, triage/hitl/domain conventions, change classes | Approved |
| Derived Views | настоящий manifest + матрицы §3-4 | Approved (manifest фиксирует финальные revision и approval records) |

## 3. Trace matrix: hazards → verification (полная таблица - `docs/verification-strategy-v3.md` §4)

Репрезентативно (полный маппинг в verification-strategy-v3.md):

| Hazard | Method | Oracle | Environment | Модуль (implementation-plan §3) |
| --- | --- | --- | --- | --- |
| HZ-01 (потеря сенсорики) | measurement + test | O4 + O1 | HIL + host | Sensing, Safety Authority |
| HZ-02 (bumper) | measurement | O4 (T_fs) | HIL | Safety Authority, CAN HAL |
| HZ-03 (отказ CAN) | measurement + commissioning | O4 | HIL + field | CAN HAL, Safety Authority |
| HZ-05/06 (деградация/I2C) | test | O1 | host (+HIL) | Sensing, I2C HAL |
| HZ-09 (stall) | test + measurement | O1 + O4 | host + L4 | Safety Authority, Sensing |
| HZ-12 (lease) | measurement + test | O4 + O1 | HIL + host | Manual Session |
| HZ-15 (update) | test + measurement | O2 + O4 | host + HIL | Bootloader/Update |

## 4. Coverage matrix: obligations → модули (из implementation-plan §4)

| Obligation (#43 §8 / #48 §11) | Модули (implementation-plan §3) | Статус закрытия |
| --- | --- | --- |
| #1 C1 chain | Safety Authority, Sensing, Actuator, CAN HAL | host-sim (#54) + L4/L5 (impl.) |
| #2 freshness суб-бюджеты | Safety Authority, Sensing | host (#54) + L4 (impl.) |
| #3 flash + ISR | Watchdog, Persistence, CAN HAL | host-sim (#54) + L4 (impl.) |
| #4 adapter bounds | HAL CAN/UART/I2C/flash | host-sim (#54) + L4 (impl.) |
| #5 watchdog | Execution core, Watchdog | host (#54) + L4/L5 (impl.) |
| #6 lease | Manual Session | host (#54) + L4 (impl.) |
| #7 queues | Observability Producer/Sink | закрыто host (#54) |
| #8 bounded steps | Execution core | закрыто host (#54, conditional) |
| #9 monotonic | Monotonic | закрыто host (#54) |
| #10 resources | build + target | map/stack/RAM host (#54); high-water impl. |
| #11 power-cut | Persistence, Bootloader/Update | L5 (impl. фаза 4) |
| #12 non-blocking TX | Observability Sink, UART | закрыто host (#54, conditional) |
| #13 CAN dual-class | CAN HAL | host (#54); физика L4/L5 (impl.) |
| #14 I2C recovery | I2C HAL, Sensing | host FSM (#54); stuck L5 (impl.) |
| #15 radio AUX | Transport radio | L5 (impl., as-needed) |
| field: D_brake/v_max | Actuator, поле | impl. фаза 3 (шаттл) |
| field: ATEMP | Sensing, ADC | impl. фаза 3 |
| field: availability | Observability | impl. фаза 4 |

## 5. G6 closure (issue 8 §6)

- [x] Baseline Manifest фиксирует main revisions и approval 13 items - profile qualification revisions items 2, 6, 7, 8, 11, 12 и 13: семантика `9b431a7` (PR #78), статус `7491869` (PR #79).
- [x] Derived views: trace (hazard → verification → модуль), status (item/class), coverage (obligations → модули).
- [x] Orphan/uncovered: каждый obligation имеет модуль и статус; каждый модуль имеет acceptance (§3 implementation-plan).
- [x] Schema-чеки: markdownlint-clean, относительные ссылки, без TBD.
- [x] Блокирующие решения: profile qualification rebaseline принят владельцем и независимым review (PR #78), тикет [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59) закрывается.
- [x] Independent review profile qualification rebaseline: APPROVE на `9b431a7` (0 BLOCKING, 0 MAJOR, 0 MINOR, 0 NIT); цепочка ревью #56/#57/4 раунда rebaseline зафиксирована в тикете #59.
- [x] Owner approval profile qualification rebaseline: утверждён на `9b431a7` (тикет #59, PR #78).

## 6. Ссылки

- Issue 8 (§2-6: классы, gates, closure), #12 (репозиторная схема), #5 (trace-модель), #52 §4 (verification attributes).
- `docs/implementation-plan-v3.md` (item 13), тикеты #56/#57.
- Evidence-ассеты (теги `evidence/*`), `docs/research/proving-slice-report.md`.
