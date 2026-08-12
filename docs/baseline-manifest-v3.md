# Baseline Manifest V3 (gate G6)

Статус: **финальный при закрытии gate G6 (тикет [#56](https://github.com/Driadix/ShuttleControllerV3/issues/56))**. Класс: Derived View (issue 8 §2: manifest и traceability/status/coverage матрицы; проверяются на полноту и консистентность, не получают повторного смыслового approval). Единая точка входа нормативного пакета V3.

Правила: изменение любого включённого item revision - по change control (#8 §9, классы Editoral/Clarifying/Semantic/Baseline-affecting); manifest сам по себе не дублирует нормативное содержание (только ревизии, статусы, gates, approval records).

## 1. Item manifest

| # | Logical item | Документ-носитель | Ревизия (commit, main) | Тикет | Gate | Статус | Approval |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Project Assurance & Governance | issue 8 resolution + `docs/agents/*.md` (tracker/triage/hitl/domain conventions) | issue 8 (immutable resolution) | #8, #7 | G0 | Approved | владелец |
| 2 | System Context & Concepts | `CONTEXT.md` + issue 2 resolution | `d92c278` | #2 | G0 | Approved | владелец |
| 3 | Stakeholder & System Requirements | распределены: `docs/semantic-contract-v3.md`, `docs/quality-attributes-and-budgets-v3.md`, per-operation docs | см. items 4/6/9 | #2, #9, #13 | G1 | Approved (распределённый) | владелец |
| 4 | Capability & Operation Contracts | `docs/semantic-contract-v3.md` + 14 algorithmic docs (#16, #30-42) | `1cd2f1b`; algorithmic docs `7873943` (группа) | #13, #16, #30-42, #9, #14 | G1 | Approved | владелец |
| 5 | Safety & Health | `docs/safety-model-v3.md` | `1b08bb8` | #45 | G2 | Approved | владелец |
| 6 | External Semantic & Transport Contracts | `docs/external-semantic-transport-contracts-v3.md` | `a2544b3` | #47 | G3 | Approved | владелец |
| 7 | Configuration, Identity & Lifecycle | `docs/configuration-identity-lifecycle-v3.md` + `docs/lifecycle-axes-v3.md` | `fe1b9ef`; `a2544b3` | #50, #46 | G2/G3 | Approved | владелец |
| 8 | Observability & Diagnostics | `docs/observability-architecture-v3.md` | `9e9c48b` | #49 | G3 | Approved | владелец |
| 9 | Quality Attributes & Budgets | `docs/quality-attributes-and-budgets-v3.md` | `1b08bb8` | #48 | G4 | Approved | владелец |
| 10 | Software Architecture | `docs/software-architecture-boundaries-v3.md` + issue 10 (execution) + `docs/proving-slice-v3.md` | `1b08bb8`; issue 10 resolution; `9c045f1` | #43, #10, #54 | G4 | Approved (execution conditional, host-only evidence) | владелец |
| 11 | Engineering & Release | `docs/engineering-and-release-baseline-v3.md` | `1b08bb8` | #51 | G5 | Approved | владелец |
| 12 | Verification Strategy & Acceptance | `docs/verification-strategy-v3.md` | `de4ca18` | #52 | G5 | Approved | владелец |
| 13 | Implementation Plan | `docs/implementation-plan-v3.md` | commit при закрытии #57 | #57 | G6 | Approved | владелец |

Evidence assets (класс Evidence, #8 §2): `docs/research/v1-system-evidence-index.md` (тег `evidence/v1-system-evidence-index`), `docs/research/v1-execution-evidence.md` (`evidence/v1-execution-evidence`), `docs/research/v3-capability-evidence-slices.md` (`evidence/v3-capability-evidence-slices`), `docs/research/proving-slice-report.md` (host-only, #54), `docs/proving-slice-v3.md` (design, #54).

## 2. Status matrix (по классам #8 §2)

| Класс | Покрытие | Статус |
| --- | --- | --- |
| Normative (13 items) | все items утверждены на своих gates | Approved |
| Evidence | V1-индексы, capability slices, proving slice отчёт | verified (independent review) |
| Governance | issue 8, triage/hitl/domain conventions, change classes | Approved |
| Derived Views | настоящий manifest + матрицы §3-4 | G6 closure |

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

- [x] Baseline Manifest фиксирует ревизии 13 items, gates, approval.
- [x] Derived views: trace (hazard → verification → модуль), status (item/class), coverage (obligations → модули).
- [x] Orphan/uncovered: каждый obligation имеет модуль и статус; каждый модуль имеет acceptance (§3 implementation-plan).
- [x] Schema-чеки: markdownlint-clean, относительные ссылки, без TBD.
- [x] Блокирующие решения: нет открытых (все тикеты карты закрыты; execution conditional зафиксирован).
- [x] Independent review: при закрытии #56/#57.
- [x] Owner approval: gate G6 (тикет #56).

## 6. Ссылки

- Issue 8 (§2-6: классы, gates, closure), #12 (репозиторная схема), #5 (trace-модель), #52 §4 (verification attributes).
- `docs/implementation-plan-v3.md` (item 13), тикеты #56/#57.
- Evidence-ассеты (теги `evidence/*`), `docs/research/proving-slice-report.md`.
