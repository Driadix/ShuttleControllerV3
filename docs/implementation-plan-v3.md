# Implementation Plan V3 (item 13, gate G6)

Статус: **Approved - profile qualification rebaseline [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59): независимый embedded/RT review APPROVE (0 findings) и owner approval на ревизии `9b431a7` (PR #78)**. Предыдущая revision Approved при закрытии [#57](https://github.com/Driadix/ShuttleControllerV3/issues/57), gate G6. Вход в logical item `Implementation Plan` нормативного пакета (issue 8, item 13, gate G6). Класс: Normative (issue 8 §2). Этот документ фиксирует трассируемую декомпозицию реализации, зависимости, acceptance gates и rollout order. Реализационный дизайн железо-завязанных модулей (движение, CAN, I2C, алгоритмы) - территория implementation-карты с evidence с реального шаттла (правило карты, Notes).

Термины - канонические из `CONTEXT.md`.

## 1. Принципы декомпозиции

1. **Foundation-first** (issue 10, validation gates): сначала execution/safety/time foundation и узкий observability/communication vertical slice; полный communication layer не проектируется целиком до функций; operation contracts добавляются вертикальными capability-слайсами.
2. **Правило карты (Notes)**: внедрение модулей постепенное; каждая операция/субоперация (ЛИФТ, ДВИЖЕНИЕ, ЗАГРУЗКА) полностью проверяется на реальном шаттле с замером таймингов; железо, PCB и настройки драйверов frozen как в V1 (аппаратный контракт); свободная переменная - только код контроллера.
3. **Модульная карта #43**: 8 domain-компонентов + 4 класса адаптеров, ports-and-adapters, dependencies внутрь; enforcement #51 §5 (раздельная native-сборка домена, include-lint).
4. **Acceptance по #52**: каждый модуль получает method/oracle/environment/evidence type; обязательные L5 acceptance - release gate; селекция регрессии по классам изменений (#8 §9).
5. **Каждая операция - вертикальный слайс**: контракт (#13/#9) + алгоритмическая документация (#16/#30-42) + host-тесты (L2/L3) + target (L4) + поле (замер таймингов, правило карты).

## 2. Фазы и rollout order

### Фаза 0: Bring-up и аппаратный контракт

- Вход: PCB-референс ControllerV6 (KiCad + BOM), V1-индекс пинов (`docs/research/v1-system-evidence-index.md`), frozen board baseline (#51 §4).
- Работы: сверка PIO board def с фактической платой (validation obligation #51 §14/§15), bring-up пинов, L4-стенд (плата + ST-Link + CAN-пир, #52 §2), диагностический мост.
- Acceptance: плата boot-ится под frozen toolchain; пины сверены; startup-to-Ready не измеряется (нет полной прошивки) - критерий перехода: исправный мониторинг + CAN-пинг + watchdog-арм.
- Выход: bring-up report (Evidence), при необходимости board JSON override (Semantic-класс, #51 §15).

### Фаза 1: Execution/safety/time foundation

- Вход: критерий перехода 0 → 1 (§5).
- Модули: execution core (cooperative, выбранный kernel из proving slice #54), monotonic time, watchdog policy, Safety Authority (health FSM, arbitration funnel, stop-профили, stall/obstacle классификация), Sensing Service каркас (включая I2C-слот-расписание), Actuator Controller каркас, HAL: monotonic/TIM, GPIO, watchdog, CAN (bxCAN: bounded TX/RX, force-stop mailbox), **I2C (ToF/AS5600, recovery механика)**, **UART (bridge/radio/BMS byte-budget каркас)**, flash (journal механика), RTC, ADC (ATEMP).
- Host: L2/L3 тесты (детерминированный core из slice, расширение); target: L4 smoke (bounded steps, watchdog, CAN loopback).
- Acceptance gate: safety-тайминги C1/T_fs на L4/L5 (обязательные acceptance #52 §6.3: 1, 2, 4, 6); bounded steps под combined load (#54 host-каркас + L4). Evidence: measurement report (C1/T_fs), CI-artifact (bounded steps).
- Критерий перехода: C1-цепочка измерена на стенде (или явно задокументированный перевод на поле), force-stop T_fs ≤ бюджет.

### Фаза 2: Observability/communication vertical slice

- Вход: критерий перехода 1 → 2 (§5).
- Модули: Observability Producer (#49: envelope, классы очередей, drop-политики, счётчики), Observability Sink (адаптер: bounded очереди, TX-планирование), Transport bridge/radio (#47: profiles network_bridge/radio, handshake, negotiation, session lease), Protocol core (#47: wire, semantic contract проекция), Manual Session, Lifecycle-оси (#46) и provisioning (#50) каркасы.
- Acceptance: контрактные wire-тесты (O2), подписочная машина (L2), radio-трафик сценарии (L2 + L5 as-needed), session lease → stop (L4/L5).
- Критерий перехода: bridge- и radio-каналы работают end-to-end на стенде; admission-матрица (#13) host-проверена.

**Gate выбора qualification target.** До перехода 2 → 3 реализация остается profile-neutral и сохраняет единый binary с bundles 800/1000/1200. На gate владелец фиксирует один физически доступный профиль как qualification target `v1.0.0`, подтверждает доступность шаттла и применимого sensor/drive setup. После gate смена target является Semantic rebaseline: профильные capability/evidence dependencies пересчитываются, затронутое evidence инвалидируется.

### Фаза 3: Capability-слайсы (13 операций)

Каждый слайс выполняется для qualification target: контракт операции (#13/#9) + алгоритмический док (#30-42) + host unit/property (O1/O2, evidence CI-artifact) + L3 с adapter fakes (CI-artifact) + L4 (measurement report) + **поле на реальном шаттле выбранного профиля с замером таймингов** (правило карты; measurement report с workload metadata). Профильные ветви других supported profiles сохраняются в универсальном binary, но не получают production claim `v1.0.0`. Порядок внутри фазы - по зависимости от foundation и от сенсорики:

| # | Операция | Док | Ключевые зависимости |
| --- | --- | --- | --- |
| 1 | MoveDistance | #30 | Sensing (ToF), Actuator (CAN 100), Safety (stall/obstacle) |
| 2 | LiftTo | #31 | Actuator (лифтер CAN 101/2405), концевики |
| 3 | Home | #39 | Sensing (одометрия), Actuator |
| 4 | LoadPallet | #32 | MoveDistance + LiftTo + паллетные сенсоры |
| 5 | UnloadPallet | #33 | MoveDistance + LiftTo |
| 6 | LongLoad / LongUnload / LongUnloadQuantity | #34/#35/#36 | Load/Unload + compact-логика |
| 7 | CompactPallets | #37 | паллетные сенсоры, позиционирование |
| 8 | CountPallets | #38 | паллетные сенсоры |
| 9 | Calibrate | #40 | одометрия, flash-калибровки (#50) |
| 10 | Demo | #41 | Load/Unload (ненормативный режим) |
| 11 | Evacuate | #42 | Safety Authority (авторизация), MoveDistance |

Критерий перехода: каждая операция принята на поле (тайминги в бюджетах #48; outcomes по контракту #13; safety-инварианты #45 не ослаблены).

### Фаза 4: Production lifecycle и release

- Модули: bootloader/update (#50: verified boot, A/B-слоты, W_apply ≤ 1 s измерение, rollback), config/provisioning полнота и image/profile qualification gate (#50), журналы/retention (#49), release gates (#52 §7.3-7.4).
- Acceptance: L5 обязательное acceptance **№5** (#52 §6.3: power-cut update E2E + W_apply измерение; все 6 обязательных - release gate), qualification negative/rollback/recovery scenarios (#52 §7.4), commissioning-процедуры выбранного профиля (Q7.1 A: fail-safe приводов, INV-BRAKE-VALIDITY), release evidence (7 пунктов #52 §7.3).
- Критерий: первый production-релиз v1.0.0 по #51 §9.2 (attestation, SHA-манифест), где оба signed app images аутентифицируют singleton `qualifiedProfileIds`, а evidence однозначно называет target profile.

## 3. Модульная карта (модуль → компонент #43 → acceptance; evidence type по #52 §4)

| Модуль | Компонент (#43) | Источники | Зависимости | Acceptance (метод/oracle/среда) | Evidence type |
| --- | --- | --- | --- | --- | --- |
| Execution core | platform | #10, #54, #48 §4 | - | test+measurement / O1,O4 / L2+L4 | CI-artifact + measurement report |
| Monotonic/Watchdog | platform | #43 §4, #48 §3 | core | test+measurement / O4 / L2+L4 | CI-artifact + measurement report |
| Safety Authority | domain | #45, #43 §3 | Sensing, Actuator, Session | test+measurement / O1,O4 / L2+L4+L5 | CI-artifact + measurement report |
| Arbitration funnel | domain | #43 §3.1, #45 §4 | - | test / O1 / L2 | CI-artifact |
| Sensing Service | domain | #43 §2, #48 §5 | I2C HAL | test+measurement / O1,O4 / L2+L4 | CI-artifact + measurement report |
| Actuator Controller | domain | #43 §4 | CAN HAL, Safety | test+measurement / O2,O4 / L2+L4+L5 | CI-artifact + measurement report |
| Config & Profile | domain | #50, #43 §4 | Persistence | test / O2 / L2 | CI-artifact |
| Observability Producer | domain | #49, #43 §6 | Transport | test / O1 / L2 | CI-artifact |
| Observability Sink | adapters | #49, #43 §6 | HAL UART | test+measurement / O1,O4 / L2+L4 | CI-artifact + measurement report |
| Semantic Contract & Admission | domain | #13, #9, #47 | Queues | test+fuzz / O2 / L2 | CI-artifact |
| Operation Runtime | domain | #13, #30-42 | все domain | test / O2 / L2+L3 | CI-artifact |
| Manual Session | domain | #13, #45 §4 | Safety | test / O1 / L2 | CI-artifact |
| HAL CAN/GPIO/TIM/I2C/UART/flash/RTC/ADC | adapters | #43 §2/§4, #48 §7, V1-индекс | - | measurement / O4 / L4 | measurement report |
| Transport bridge/radio | adapters | #47, #48 §7 | HAL UART | test+measurement / O2,O4 / L2+L4+L5 | CI-artifact + measurement report |
| Persistence (journal) | adapters | #43 §4, #50 | HAL flash | test+measurement / O2,O4 / L2+L4+L5 | CI-artifact + measurement report |
| Bootloader/Update | platform | #50 | Persistence, flash-карта | test+measurement / O2,O4 / L2+L4+L5 | CI-artifact + measurement report |

## 4. Трассировка obligations → модули

- **#43 §8 (#1-#15)**: #1/#2/#3 → Safety Authority + Sensing + Actuator + CAN HAL (C1-цепочка); #4 → адаптеры; #5 → Watchdog + flash; #6 → Manual Session; #7 → Queues/Observability; #8 → Execution core; #9 → Monotonic; #10 → build+target; #11 → Update/Persistence (L5); #12 → Observability Sink + UART; #13 → CAN HAL; #14 → I2C HAL + Sensing; #15 → Transport radio.
- **#48 §11 measurement obligations**: полевые (#1 D_brake/v_max, ATEMP, availability) - фаза 3/4 на реальном шаттле; bench (#2-#8/#10/#12-#14) - фазы 1-3 на L4; HIL (#11/#13/#15) - фаза 4 (L5).
- **#49 §13 (observability obligations)**: fault-capture ≤ 512 Б, integrity-скан/journalFull, лимиты журнала/wrap/SetWallClock plausibility, backup-domain loss → Observability Producer + Persistence; подписки/gap re-sync, snapshot ≤ 2 фрагмента, radio events-резерв → Observability Producer + Transport bridge/radio; crash-запись через reboot, reset-cause счётчики → Observability Producer + Execution core (стартап).
- **#50 §12 (lifecycle contract tests)**: load_address mismatch, Finalize/commit counter семантика, DowngradeDenied, rollback_flag, status sector-full, JournalFull, power-cut mid-provisioning/mid-GC, verified boot/bootcount/pointer/pending, `ProfileNotQualified`, image/configured-profile admission, mismatch Recovery → Bootloader/Update + Config & Profile + Persistence.
- **#52**: startup-to-Ready ≤ 5 s (INV-STARTUP-GATE) → Execution core + Safety Authority (стартап, L4); обязательные L5 (§6.3) → фазы 1/2/4.
- **#52 verification attributes**: каждый модуль §3 таблицы имеет method/oracle/environment/evidence type; обязательные L5 - фазы 1, 2, 4.

## 5. Rollout criteria (переходы между фазами)

| Переход | Критерий |
| --- | --- |
| 0 → 1 | bring-up report принят; мониторинг/пинг/CAN работают |
| 1 → 2 | C1-цепочка и T_fs измерены (L4/L5 или явный перевод на поле); bounded steps подтверждены |
| 2 → 3 | каналы end-to-end на стенде; admission host-проверена; физически доступный профиль и sensor/drive setup зафиксированы как qualification target `v1.0.0` |
| 3 → 4 | 13 операций приняты на поле с таймингами; safety-инварианты не ослаблены |
| 4 → release | L5 обязательные 6 acceptance (#52 §6.3) + Profile Qualification Campaign выбранного профиля (#52 §7.4) + release evidence 7 пунктов |

Откат фазы (rule #8 §9 change classes): любой Semantic-дефект в модуле фазы N возвращает фазу N по impact-анализу, не откатывая foundation.

## 6. Риски и зависимости

- **Железо**: bring-up и полевые замеры требуют платы и шаттла (implementation-карта); до них - L2/L3 host-слой (база из slice #54).
- **Стенд L5**: обязательные acceptance требуют стенда #52 §6; при недоступности - деградация на bench с документированием (#52 §12).
- **Приводы/лифтер**: контракты CAN 100/101/2405 и fail-safe (Q7.1 A) проверяются commissioning-тестами на поле.
- **W_apply ≤ 1 s**: измерение на L5 (фаза 4); RAM-exec развилка - по измерению flash-стопа.
- **Профили 800/1000/1200**: единый binary реализует все три supported bundles; `v1.0.0` квалифицирует один target, выбранный на gate 2 → 3. Геометрия и calibration-пороги target (U07/U01) уточняются при commissioning. Остальные профили требуют отдельных future minor-release campaigns; qualification переносится между релизами только по impact analysis.
- **F4 (открытый, карта #1 fog)**: реализуемость и реальная необходимость min-ID force-stop кадра (поддержка приводами) не подтверждена железом; при неподдержке C4 переякоривается на verified drive-timeout (Q7.1 A расширяется). Фаза 1 acceptance gate (обязательное №2: force-stop кадр ≤ T_fs) зависит от F4 - закрытие F4 или явный перевод в implementation-карту до входа в фазу 1.

## 7. G6 closure checklist (issue 8 §6)

- [x] Pre-rebaseline revision: нет TBD и открытых блокирующих решений в исходном scope (все решения карты #1 приняты; F4 - открытый риск §6, закрывается до входа в фазу 1 или переводится явно).
- [x] Каждый модуль имеет источник, зависимости и acceptance (таблицы §3-4).
- [x] Обязательные сценарии (safety-путь C1, fault-пути, overload, update) покрыты acceptance.
- [x] Трассировка obligations/бюджетов/verification → модули полна (§4, включая #49 §13 и #50 §12).
- [x] Derived views (trace/status/coverage) - тикет #56 (Baseline Manifest).
- [x] Pre-rebaseline revision: independent review проведен при закрытии тикета #57 (2 MAJOR + 4 MINOR + 2 NIT учтены).
- [x] Profile qualification rebaseline: независимый embedded/RT review APPROVE (0 findings) и owner approval на ревизии `9b431a7` (PR #78), тикет [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59) закрыт.

## 8. Ссылки

- `docs/proving-slice-v3.md`, `docs/research/proving-slice-report.md` (host-only evidence, выбор execution).
- `docs/software-architecture-boundaries-v3.md` (#43), `docs/quality-attributes-and-budgets-v3.md` (#48), `docs/safety-model-v3.md` (#45), `docs/verification-strategy-v3.md` (#52), `docs/engineering-and-release-baseline-v3.md` (#51), `docs/configuration-identity-lifecycle-v3.md` (#50), `docs/observability-architecture-v3.md` (#49), `docs/external-semantic-transport-contracts-v3.md` (#47), `docs/lifecycle-axes-v3.md` (#46), `docs/semantic-contract-v3.md` (#13).
- Алгоритмические доки: `docs/*-algorithmic-documentation-v3.md` (#16, #30-42).
- Issue 8 (item 13, G6), тикет #57.
