# Verification Strategy & Acceptance V3 (Verification Pyramid, item 12)

Статус: **Approved - profile qualification acceptance rebaseline [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59): независимый embedded/RT review APPROVE (0 findings) и owner approval на ревизии `9b431a7` (PR #78)**. Предыдущая revision утверждена владельцем и независимо reviewed в тикете [«Спроектировать verification pyramid V3 (item 12)»](https://github.com/Driadix/ShuttleControllerV3/issues/52). Вход в logical item `Verification Strategy & Acceptance` нормативного пакета (issue 8, gate G5) и в architecture proving slice (тикет #54). Каждый метод, oracle и бюджет имеет источник; бюджеты-лимиты - из `docs/quality-attributes-and-budgets-v3.md` (#48).

Термины — канонические из `CONTEXT.md`. Исполнение — cooperative scheduler с bounded run-to-completion steps (issue 10), ports-and-adapters (#43), safety-модель (#45), наблюдаемость (#49), production lifecycle (#50).

## 1. Назначение и входы

Документ фиксирует verification pyramid V3: уровни/методы/среды, oracle-модель, распределение validation obligations (#43) и measurement obligations (#48) по уровням без потерь, HIL-стенд и fault-injection, acceptance evidence и manual approval, regression strategy. Закрывает closure G5 (issue 8): каждому requirement и hazard назначены method, oracle, environment и evidence type; HIL/fault-injection, update/recovery и release suites специфицированы.

Входы: #43 (15 validation obligations, §8), #48 (measurement obligations, §11; бюджеты), #45 (hazards, invariants, verification obligations), #49 (obligations наблюдаемости, §13), #50 (контрактные тесты lifecycle, §12), issue 10 (evidence-список proving slice), #51 (host-тесты, static analysis, CI, release), issue 8 (item 12, G5, evidence class), #53 (evidence assets с source/version/confidence).

## 2. Пирамида: уровни, среды, методы

Пять уровней + полевая зона. Пирамида — правая ветвь V-model (ISO 26262-6: unit → integration → embedded software testing; лестница MIL/SIL/PIL/HIL в терминологии model-based development).

| Уровень | Что проверяется | Среда | Метод | Каденция |
| --- | --- | --- | --- | --- |
| **L1 document/schema checks** | структура пакета, trace-матрицы (derived views), schema-валидация контрактов (#47 wire/config), review-checklist (#51 §6.3), include-lint, dependency-матрица, формат/статический анализ | CI (ubuntu-24.04) | automated checks + independent review | каждый PR |
| **L2 host unit/property** | domain core native: unit-тесты, property-тесты инвариантов (O1), контрактные unit-тесты (O2), fuzz парсера, FSM-логика (stall/lease/recovery/health/evacuation), wrap-семантика | CI (host, `[env:native]`, GoogleTest + RapidCheck) | test + property-based test | каждый PR |
| **L3 host integration** | домен + adapter fakes: сквозные сценарии операций (13 algorithmic docs), admission-матрица end-to-end, очередь/overload, fault-capture assembly, контрактные wire-тесты | CI (host, native) | integration test (симулированные интерфейсы) | каждый PR |
| **L4 target tests (bench)** | плата без механики: bring-up, startup-to-Ready ≤ 5 s (O4), verified boot, bootcount/pointer/pending, журналы (integrity-скан, crash-запись через reboot), reset-cause, backup-domain loss, watchdog под нагрузкой, adapter duration bounds, bounded steps под load, high-water/CPU margin, O3 back-to-back (host-векторы на target, тестовая сборка) | bench (плата + ST-Link, поверхность bridge) | test + measurement | nightly |
| **L5 HIL (стенд)** | 6 обязательных acceptance (раздел 6) + as-needed сценарии fault-injection | стенд (раздел 6) | measurement + fault-injection test | release + Semantic safety-путь |
| **field** | полный шаттл: E2E операции с замером таймингов, D_brake, v_max_phys, ATEMP, availability | реальный шаттл | measurement + commissioning | implementation-карта (перенос) |

Решения владельца:

- **L3 = host-интеграция с adapter fakes**; instruction-set simulation (Renode/QEMU) и plant model **отклонены** (Q1, SIL-вопрос). L3 выполняет роль software-in-the-loop слоя: production-логика исполняется на host против симулированных интерфейсов (fakes); слово «simulation» в названии уровня означает симуляцию интерфейсов, а не физики.
- **Граница L4/L5**: L4 — плата без механики; L5 — стенд с fault-injection и управляемым питанием; полный шаттл — field-зона implementation-карты (правило карты: каждая операция проверяется на реальном шаттле при переносе).
- **Состав bench (L4)**: плата + ST-Link; CAN-пир или инъектор (adapter duration bounds, watchdog под нагрузкой), I2C-сенсоры или симулятор, нагрузочный скрипт по bridge; захват таймингов — периодические события с monotonicTick и временные метки (envelope #49 §2.1).
- **O3 на L4 (механизм)**: доменные векторы исполняются в тестовой сборке (зеркальные флаги #51 §4) с отчётом через bridge; производственная сборка — только L5 (там oracle O4/железо).

## 3. Oracle-модель (Q2)

Четыре класса oracle; V1 **не является** oracle (правило карты: V1 — свидетельства, не норматив).

| Oracle | Что является источником истины | Где применяется |
| --- | --- | --- |
| **O1 инвариантный/property** | safety-инварианты INV-* (#45 §3), инварианты очередей/overload (#43 §6), single-writer ownership, wrap-семантика monotonic (#49 §2.1) | L2 (PBT), L3 (сквозные) |
| **O2 контрактный/reference** | semantic contract (#13: admission, outcomes, lifecycle), protocol schema (#47), алгоритмическая документация (#15, #16, #30–42: инвариант Succeeded ⇔ цель достигнута, классификация путей) | L2/L3 (контрактные тесты, wire-тесты) |
| **O3 host-target equivalence** | одинаковые тестовые векторы на native и target-сборке; расхождение доменного поведения = дефект (back-to-back, ISO 26262-6) | L2→L4 (перенос векторов) |
| **O4 measured-vs-budget** | observed maxima с workload metadata (протокол кампании — §6.3) vs бюджеты #48 + margin (NASA SWEHB 9.12/9.14 практика); на HIL — реакция vs limit (практика Rockwell/Siemens, ISO 13849-2) | L4/L5 (тайминги, ресурсы) |

Инструменты:

- **RapidCheck** для property-тестов (header-only, C++17, dev-only зависимость, интеграция с GoogleTest; версия пинится по политике #51 §3). При флейках генераторов в CI — фиксированные seed'ы.
- **Структурированный fuzz парсера кадров** + admission-путь на host (метод L2/L3, oracle O1/O2: инварианты + не-краш; практика ECUFuzz, ISSTA 2025). Fuzz-гарнесы — dev-only, вне production-сборки.

Критерий прохождения: тест проходит по своему oracle; тайминговые тесты — по O4 (measured ≤ бюджет + margin); analytical-оценки не называются measured (#48 §1).

## 4. Назначение verification attributes (G5 closure)

Каждый requirement и hazard получает строку verification matrix (derived view, issue 8): `method · oracle · environment · evidence type`.

- **Method**: review (independent) / automated checks & static analysis (lint, schema-валидация, include-lint; в ISO 26262-6 inspection — ручная техника, у нас она покрыта review) / analysis (source/static proof) / test (host/target) / measurement (тайминги, ресурсы) / commissioning (ручной per-device).
- **Oracle**: O1–O4 (раздел 3).
- **Environment**: CI-host / bench / HIL / field.
- **Evidence type**: CI-artifact (JUnit/JSON/logs) / evidence record (стабильный, docs) / measurement report (observed maxima + workload metadata) / commissioning record (внешний журнал ТО) / review record.

Матрица requirement→verification наполняется при сборке пакета (G1–G5); каркас нормативен. Hazard-каталог (#45 §1.2) маппится по механизму ниже (примеры):

| Hazard | Method | Oracle | Environment | Evidence type |
| --- | --- | --- | --- | --- |
| HZ-01 (потеря сенсорики) | measurement + test | O4 (T_eso ≤ 70 ms) + O1 (INV-SENSING-FRESH); D_accept (D_brake, v_max_phys) — field (§5.2) | HIL + host (+ field) | measurement report + CI-artifact |
| HZ-02 (bumper/столкновение) | measurement | O4 (T_fs ≤ 10 ms, latch, crash counter) | HIL | measurement report |
| HZ-03 (отказ CAN) | measurement + commissioning | O4 + per-device commissioning-тест (Q7.1 A) | HIL + field | measurement report + commissioning record |
| HZ-04 (инверсия команды) | test | O1 (property: переходы, admission) | host | CI-artifact |
| HZ-05/06 (деградация сенсорики/I2C) | test | O1 (health FSM, INV-SENSING-FRESH, T_deg) | host (+HIL: stuck, as-needed) | CI-artifact |
| HZ-07/08 (лифтер) | test | O2 (алгоритмическая документация, timeout) | host | CI-artifact |
| HZ-09 (stall) | test + measurement | O1 (INV-STALL-NO-PROGRESS) + O4 (W_np) | host + L4 | CI-artifact |
| HZ-10 (low-battery) | test | O1 (INV-LOW-BATTERY, reaction FSM) | host | CI-artifact |
| HZ-11 (позиционирование) | test | O2 (per-operation алгоритмы #30–42) | host | CI-artifact |
| HZ-12 (lease loss) | measurement + test | O4 (T_lease_stop) + O1 (INV-LEASE-STOP) | HIL + host | measurement report + CI-artifact |
| HZ-13/14 (конфигурация/журнал) | test | O2 (валидация, JournalFull, HZ-14-путь) | host (+L5: power-cut, as-needed) | CI-artifact |
| HZ-15 (update) | test + measurement | O2 (rollback-контракт) + O4 (W_apply ≤ 1 s) | host + HIL | CI-artifact + measurement report |
| HZ-16 (перегрев) | test | O1 (health FSM, пороги 90/110 °C) | host (+field: телеметрия) | CI-artifact |
| HZ-17 (BMS staleness) | test | O1 (staleness-логика, T_bms_stale = 120 s) | host | CI-artifact |

Инварианты без прямого hazard-хозяина: **INV-EVACUATION-BOUNDED** (эвакуация — авторизованный bounded SAFETY_MOTION, доступен в Fault, #45 §4) — O1 property (границы, авторизация Safety Authority, bounded метаданные), host, CI-artifact (L2); INV-WATCHDOG-ARMED — покрыт acceptance #4 (§6.3) и host-инвариантами reload-политики; INV-STARTUP-GATE — startup-to-Ready ≤ 5 s (L4).

## 5. Распределение obligations (Q3)

Принцип (решение владельца): **obligations закрываются proving slice #54 одноразово как evidence; в production-пирамиду входят обязательные acceptance-тесты (safety-тайминги HIL, release gate) и регрессия по необходимости (по классу изменения)**. Распределение без потерь: каждая obligation имеет точку закрытия.

### 5.1 Proving slice #54 (одноразовое закрытие, evidence records)

Все 15 obligations #43 §8 / #48 §11 закрываются измерениями #54 (observed maxima с workload metadata, comparison report по трём kernel variants, evidence issue 10: source/static proof, link map, per-function stack, heap policy, high-water, CPU margin): #1 event→safe-output, #2 freshness суб-бюджеты, #3 flash-stall + ISR latency + force-stop в окне erase, #4 adapter duration bounds, #5 watchdog combined load, #6 lease→safe stop, #7 очередь overload, #8 bounded steps, #9 NTP-скачок (host property), #10 RAM/stack/link-map, #11 power-cut (save/update/mid-operation), #12 неблокирующий TX (log-storm), #13 CAN dual-class TX/RX/flood, #14 I2C recovery при BMS, #15 radio AUX-hang.

### 5.2 Полевые obligations (implementation-карта)

D_brake(v, load, grade), v_max_phys (percent→м/с mapping привода), ATEMP-пороги при типовой нагрузке, availability ≥ 99.5% — полевые измерения при переносе функционала; результаты — release-evidence поля (measurement records с workload metadata).

### 5.3 Production-пирамида: обязательное и по необходимости

| Obligation (источник) | Регрессионный статус | Где |
| --- | --- | --- |
| C1 event→safe-output, force-stop T_fs, lease T_lease_stop, watchdog starvation, power-cut update E2E + W_apply, CAN flood → control-plane | **mandatory** (release gate) | L5 (раздел 6) |
| Host-логика obligations: #9 NTP-скачок; #49 #4 (fault-capture assembly ≤ 512 Б), #6 (integrity-скан, journalFull), #7 (подписки/gap re-sync), #9 (лимиты журнала, wrap, plausibility SetWallClock, RFC 5424); #50 host-часть (Finalize-валидация вкл. load_address mismatch; counter-семантика: Finalize не повышает accepted_counter, commit повышает атомарно; rollback к меньшему counter → DowngradeDenied; FactoryReset generation-bump; ClearFault admission; JournalFull; power-cut mid-provisioning → старый снапшот) | постоянная (реализуется как unit/property-тесты при реализации модулей) | L2 |
| #49 #5 (crash-запись через reboot), #8 (reset-cause счётчики), #11 (backup-domain loss); #50 target-часть (verified boot, bootcount, pointer/pending/commit, персистированный фолбэк bootloader — указатель == исполняемый слот, status sector-full, rollback_flag → событие rollback-причина) | постоянная | L4 |
| #49 #10 (radio-трафик: events-резерв не вытесняется telemetry-подпиской; snapshot-ответ radio ≤ 2 фрагмента) | постоянная (host: подписочная машина, fake link) + as-needed (HIL radio-трафик) | L2 + L5 |
| I2C stuck при BMS (#14), radio AUX-hang (#15), log-storm (#12), power-cut save/mid-op, BMS low-SOC, flash-stall ISR latency (#3), CAN физический слой | **as-needed** (по классу изменения) | L5 |
| #4 adapter duration bounds, #7 queue overload (наблюдение high-water), #8 bounded steps, #10 high-water/CPU | as-needed (по классу) | L4 |

## 6. HIL-стенд и fault-injection (Q4)

### 6.1 Состав стенда

- Плата контроллера (production-прошивка, ST-Link);
- **CAN-инъектор/анализатор**: flood, ошибки физического слоя (обрыв/КЗ/терминация), протокольная инъекция, наблюдение force-stop (min extended ID) с timestamp; trigger-кадр для синхронизации измерений;
- **Программируемое питание + реле-коммутатор**: power-cut в заданных точках, brown-out dips;
- **I2C-коммутатор**: stuck SDA/SCL (привязка линии к GND), разрыв устройств;
- **Парный E22-модуль** + RF-аттенюатор: AUX-hang, mode-settle, потеря линка;
- **Опциональный bench лифтера/приводов** (механика не обязательна для L5);
- BMS-симулятор для low-SOC-сценариев (as-needed).

### 6.2 Карта fault-injection (сценарий → oracle)

| Сценарий | Инъекция | Oracle |
| --- | --- | --- |
| CAN flood | поток > 64 кадров/тик | drop + счётчик + событие; control-plane жив; T_fs не нарушен |
| CAN физический слой | обрыв/КЗ/терминация | error counters, bus-off recovery, stop при error passive (HZ-03) |
| Force-stop | bumper edge (в т.ч. в окне erase) | вне окна: min-ID кадр ≤ T_fs + latch + crash counter; в окне erase: эмиссия отложена до конца окна, обработка latch ≤ 4 s после окна (Q7.2), crash counter может недосчитать (принятый риск) |
| Stuck I2C | SDA/SCL к GND при BMS-транзакции | recovery-механика работает (≤ 16 SCL, STOP, cooldown ≥ 5 s); своевременный Degraded (HZ-06) + Stop при влиянии на motion-сенсорику (INV-SENSING-FRESH); T_deg-отсчёт; после recovery — квалифицированный выход. Свежесть в окне stuck **не удерживается** (образцов нет) — это и есть условие перехода |
| Log storm | поток логов по bridge | ни один шаг > T_step; drop-newest + счётчик (неблокирующий TX) |
| Power-cut | снятие питания в точке: save / staging / applying / mid-op / mid-GC s7 | валидный предыдущий снапшот; bootcount++; rollback; W_apply ≤ 1 s; HZ-14-путь |
| Radio AUX-hang | удержание AUX | AUX-ожидание неблокирующее (split по bounded под-шагам, ни один шаг > T_step = 10 ms); mode-settle в бюджете тика; при hang — таймаут + backoff 5/30/120/600 + событие/счётчик |
| Radio-трафик (as-needed) | telemetry-подписка активна + events-поток по radio | events-резерв не вытесняется telemetry (Q3-A radio-поверхность); snapshot-ответ ≤ 2 фрагмента; manual-сессия занимает radio-слот, tear-down на Closing (#49 #10) |
| Watchdog starvation | отъём reload (тест-хук) | reset в аппаратном окне 6.8–18.8 s; boot-причина watchdog; crash-запись |
| Lease loss | прекращение hold-to-run | T_lease_stop measured ≤ бюджет |
| BMS low-SOC | BMS-симулятор, fresh sample ≤ порог | SAFETY_MOTION bounded return → CONTROLLED + fault (HZ-10) |

### 6.3 Обязательные acceptance (release gate, Q3-B/Q4)

1. C1-цепочка: event→safe-output, T_eso measured (trigger→stop timestamp);
2. Force-stop: bumper → min-ID кадр ≤ T_fs + latch + crash counter;
3. Lease: expiry → T_lease_stop ≤ бюджет;
4. Watchdog: starvation → reset в аппаратном окне + boot-причина;
5. Power-cut update E2E (staging/applying) + W_apply ≤ 1 s;
6. CAN flood → control-plane целостность + T_fs не нарушен.

Каждое измерение: observed maxima с workload metadata (какие прерывания, CAN-нагрузка, состояние движения), не WCET.

**Reference instant и синхронизация часов.** Trigger-момент фиксируется корреляцией `monotonicTick` ↔ таймстамп анализатора: trigger-кадр по CAN + событийные метки с tick в envelope (#49 §2.1). Для C1-цепочки измеряются два уровня: полная цепочка «последний валидный образец → стоп-кадр» ≤ T_fresh + T_eso = 370 ms и «детекция staleness → стоп» ≤ T_eso = 70 ms (декомпозиция по событийным меткам). Для W_apply отсчёт — от подтверждённой quiescence (#50 §3.1): внешняя детекция фазы по событийным меткам Update-этапов. Power-cut-точки (save/staging/applying/mid-op/mid-GC) — по меткам фаз с monotonicTick.

**Протокол observed-maxima.** Минимальный объём кампании: ≥ 30 прогонов на сценарий (стартовый минимум; планировщик детерминированный — разброс дают нагрузочные паттерны, поэтому workload-матрица важнее N; калибруется по стабильности max на #54). Workload-матрица фиксируется (активные прерывания, CAN-нагрузка, состояние движения, занятые очереди). Правило решения: measured max за N прогонов + margin ≤ бюджет; margin — из #48 (C1a ≥ 100 ms; watchdog-практика k ≥ 1.5–2), иначе задокументированный запас ≥ 20% или явный отказ от запаса с обоснованием. Analytical-состав T_fs = T_isr + T_step + T_mailbox остаётся аргументом полноты путей (гибрид static+measurement, NICTA).

## 7. Acceptance evidence и manual approval (Q5)

### 7.1 Evidence records

Каждый verification activity → evidence record: `{ID (V-<n>), тип (review/analysis/test/measurement/commissioning), refs (requirements/hazards/obligations), method, oracle, environment, результат, source/version/confidence, owner, дата}`. Автоматические прогоны — CI-артефакты (JUnit/JSON/logs), на которые records ссылаются. Паттерн #53/#55 (main-resident evidence assets с относительными ссылками; commit-pinning - только для внешних ассетов; исторические ссылки защищены тегами `evidence/*`) + формализация полей. Измерения — с workload metadata.

### 7.2 Approval

- **Automated checks** (green CI) = evidence, не approval;
- **Independent review** — обязателен для Semantic-изменений (#8 §9) и для закрытия verification obligations (review evidence); независимость I1 (ISO 26262-6 confirmation measure);
- **Owner approval** — release gate, residual risks (hazard acceptance, G2), waivers, non-blocking obligations (owner + deadline/stage + method + условие invalidation);
- **Commissioning** (brake/force-stop тест Q7.1 A, INV-BRAKE-VALIDITY) — ручной per-device sign-off во внешнем журнале ТО; firmware предоставляет авторизованные примитивы и диагностику, регистрация — вне firmware; в CI не входит.

### 7.3 Release evidence (к каждому релизу `v*`)

1. CI green по всем jobs (toolchain/docs/format/static/host-tests/build/codeql);
2. Host test report (JUnit/JSON) + static analysis report;
3. L5 acceptance report: 6 обязательных acceptance + as-needed прогоны (workload metadata);
4. Link map / per-function stack / CPU / RAM high-water report;
5. Evidence refs: закрытые obligations #43/#48/#49/#50;
6. Build artifacts + attestation (#51 §11) + `SHA256SUMS.txt` + `PROFILE-QUALIFICATION.json` (#51 §9.2);
7. Review records (включая verification-evidence review) + owner sign-off.

### 7.4 Profile qualification evidence

- Каждый release независимо объявляет `qualifiedProfileIds`, аутентифицированный подписью app image. Внешний `PROFILE-QUALIFICATION.json` (#51 §9.2) зеркалирует множество, связывает оба slot-артефакта с их SHA256 и evidence records и включается в `SHA256SUMS.txt`; он является evidence, а не trust root.
- `v1.0.0` квалифицирует ровно один профиль. Добавление профиля в future minor-release требует trace-based Profile Qualification Campaign: полный профильный L5/HIL и field/commissioning набор; все safety timing measurements и capability boundary scenarios, зависящие от геометрии, массы, приводов, торможения, sensor placement, calibration или tuning; полный host regression и обязательные release checks нового binary.
- Profile-neutral evidence переиспользуется только через trace impact analysis. Qualification не наследуется автоматически по semver: для каждого переносимого профиля повторяются затронутые физические проверки плюс общий regression, а незатронутое evidence переиспользуется ссылкой.
- Обязательные negative/lifecycle сценарии каждого release: одинаковое `qualifiedProfileIds` в app_A/app_B; metadata покрыта подписью; `ProfileNotQualified` не меняет RAM, journal и lifecycle при initial/re-provisioning; power-cut не создает partial profile state; target-image admission сверяет persisted `configuredProfileId`; несовместимый manual rollback/update отклонен; несовместимый auto-fallback сохраняет configured profile, оставляет active profile отсутствующим и дает `Recovery` + motion lock; совместимый recovery update восстанавливает active profile и Serving.

## 8. Regression strategy (Q6)

| Событие | Прогон |
| --- | --- |
| PR/merge | L1 + L2 + L3 (весь host-слой, CI) |
| Nightly | L4 bench-suite (bounded steps, high-water, журналы, verified-boot smoke) |
| Release `v*` | L5 обязательные 6 acceptance + as-needed по impact + полный L4 + host-слой + Profile Qualification Campaign/impact carryforward (§7.4) для каждого заявленного профиля |
| Hotfix/patch | trace-based селекция (тесты затронутых модулей) + обязательные L5 при изменении safety-пути |

Селекция по классу изменения (#8 §9):

- **Editorial** → L1;
- **Clarifying** → L1 + затронутые host-тесты;
- **Semantic** → полный host-слой + L4 + as-needed L5 по impact-анализу (какие obligations/бюджеты затронуты) + обязательные L5 при любом изменении safety-пути (Safety Authority, sensing, arbitration, CAN force-stop, watchdog, flash-журнал);
- **Baseline-affecting evidence** → impact review + повторное закрытие obligations по необходимости.

Правила:

- HIL-время: release gate + Semantic изменения safety-пути; ночные HIL-кампании — по накоплению/свободе стенда;
- Flaky-тесты — quarantine (не gate-block), триаж bench-дефектов vs product-багов (практика OPAL-RT);
- Назначение тестов по trace: каждый тест знает, какие модули/обязательства покрывает (для trace-based селекции и trace-матрицы).

## 9. Покрытие (coverage policy)

- **Trace-матрица** (derived view, issue 8): 100% mapping requirement/hazard ↔ verification attributes (method/oracle/environment/evidence type) — обязательна на каждом gate;
- **Host branch-coverage** (gcov на native) для safety-компонентов домена (Safety Authority, Sensing staleness, arbitration, lease, recovery FSM) — release evidence; стартовый порог 0%, целевой ≥ 90% на release, порог растёт по мере накопления тестов;
- MC/DC и target-coverage — вне scope (internal profile, не сертификация; решение #51);
- Property-тесты (O1) покрывают инварианты независимо от структурного покрытия.

## 10. Решения владельца (Q1–Q6 + SIL)

| ID | Решение |
| --- | --- |
| Q1 | Геометрия: L1 docs/schema, L2 host unit/property, L3 host-интеграция с adapter fakes (без ISS и plant model), L4 bench (плата без механики), L5 HIL-стенд; полный шаттл — field implementation-карты |
| Q2 | Oracle-модель O1–O4; RapidCheck для PBT; структурированный fuzz парсера (L2/L3); V1 не oracle |
| Q3 | Obligations закрываются #54 одноразово (evidence); production-пирамида = обязательные acceptance (safety-тайминги HIL) + регрессия по необходимости |
| Q4 | Полный стенд (CAN-инъектор, питание-реле, I2C-коммутатор, E22+аттенюатор); 6 обязательных acceptance (release gate) + as-needed сценарии |
| Q5 | Evidence records {ID, тип, refs, method, oracle, environment, результат, source/version/confidence, owner}; automated ≠ review ≠ owner; commissioning — ручной внешний sign-off; release evidence — 7 пунктов |
| Q6 | Три каденции (PR: host-слой; nightly: L4; release: L5 + as-needed) + селекция по классу изменения + правило HIL-времени |
| SIL | Plant model (строгий SIL) отклонён: fidelity-потолок до полевых данных (#48 field), тайминги не покрывает, дублирует L3; L3 выполняет роль SIL-слоя через fakes |

## 11. Assumptions / Unknowns / Confidence

- **Assumption:** host↔target эквивалентность домена держится на зеркальных build flags (#51 §4) и отсутствии UB (R7); расхождения — баги или toolchain-семантика (O3-триаж).
- **Assumption:** стенд HIL доступен на implementation-карте; ночное L4-окно доступно.
- **Assumption:** PBT-генераторы детерминированы (seed-политика при флейках); RapidCheck и googletest версии фиксируются при стендапе native env (#51 §3, §8).
- **Assumption:** AUX 20 ms (#48 §5) — конфигурированное значение; соответствие T_step = 10 ms обеспечивается неблокирующим ожиданием (split по под-шагам, #43 §4 «никогда не блокировать»), проверяется сценарием AUX-hang (раздел 6.2).
- **Assumption:** объём observed-maxima кампании (N ≥ 30) — стартовый, калибруется по стабильности max на #54; workload-матрица фиксируется по образцу #54 (synthetic loads issue 10).
- **Unknown:** фактический объём trace-записи журнала (s11, лимиты 5/25) и подписочных caps — по опыту #54/#52 (карта #1, fog).
- **Unknown:** точные версии googletest framework при стендапе native env (фиксируются по факту, #51 §14).
- **Confidence:** высокая для host-слоя (механизмы #51 проверены); целевые покрытия и пороги — стартовые, калибруются опытом; HIL-обязательства — на этапе спецификации (стенд материализуется на implementation-карте).

## 12. Условия пересмотра

- Рост доли HIL-only дефектов на release-кампаниях выше порога → обязательные L5 на ночные Semantic-прогоны;
- Систематические host/target-расхождения не-баговой природы → сужение O3 до домена;
- Стенд HIL недоступен на implementation-карте → обязательный набор деградирует на bench (watchdog/lease с программным starvation) с документированием;
- Переход к сертификации → миграция evidence-схемы на ISO 26262-6 work products;
- Появление зрелого cycle-accurate ISS с поддержкой ststm32/Arduino core → пересмотр решения по ISS;
- Новый профиль шаттла → отдельный minor-release, пересчёт D_brake, геометрии, stall-порогов (#48 §12) и Profile Qualification Campaign (§7.4);
- Изменение общего кода, safety policy, tuning или аппаратных assumptions → повторный impact analysis qualification всех заявленных профилей; неподтвержденный профиль исключается из `qualifiedProfileIds` этого release;

## 13. Отклонённые альтернативы

| Вариант | Отклонено | Основание |
| --- | --- | --- |
| Instruction-set simulation (Renode/QEMU) | Q1 | слабая поддержка Arduino core/F405, тайминги нерепрезентативны, дублирует L4 |
| Plant model (строгий SIL) | SIL-вопрос | fidelity-потолок до полевых данных; тайминги не покрывает; L3-fakes + O2-сценарии + field E2E покрывают потребность |
| Отдельный SIL-уровень | SIL-вопрос | избыточен: модель требует валидации как артефакт, дублирует L3 |
| Golden V1 как oracle | Q2 | V1 не норматив (правило карты) |
| Только requirement-based тесты без PBT | Q2 | слабее для инвариантов, случайные последовательности не покрыты |
| Без fuzzing парсера | Q2 | ECUFuzz-класс дефектов остаётся на поле |
| Полное закрытие obligations только #54 без регрессии | Q3 (владелец принял B — см. §5.3) | обязательные acceptance + as-needed сохранены |
| Вся регрессия на каждом PR (включая L4/L5) | Q6 | стендовое время нереалистично (практика: smoke на merge) |
| Без nightly L4 | Q6 | bench-дефекты живут до релиза |
| Минимальный стенд (без I2C/radio-инъекции) | Q4 | физика HZ-06/HZ-03 непроверена до поля |
| Все fault-injection сценарии обязательны | Q4 | стендовое время на релиз неоправданно |
| Только CI-артефакты без evidence records | Q5 | G5 closure и трассировка obligations не закрываются |
| Полный ISO 26262-6 work-product набор | Q5 | избыточен для non-certified проекта |

## 14. Ссылки

- Тикет «Спроектировать verification pyramid V3 (item 12)» (#52) и resolution-комментарии Q1–Q6.
- #43 (§8 — 15 validation obligations), #48 (бюджеты, §11 — measurement plan), #45 (hazards/инварианты/verification), #49 (§13 — obligations наблюдаемости), #50 (§12 — контрактные тесты lifecycle), issue 10 (evidence-список proving slice), #51 (engineering baseline: host-тесты, static, CI, release), issue 8 (item 12, G5, evidence class, change classes), #53 (evidence assets).
- `docs/quality-attributes-and-budgets-v3.md`, `docs/safety-model-v3.md`, `docs/observability-architecture-v3.md`, `docs/configuration-identity-lifecycle-v3.md`, `docs/engineering-and-release-baseline-v3.md`, `docs/software-architecture-boundaries-v3.md`, `docs/research/v3-capability-evidence-slices.md`.
- Индустриальные источники: ISO 26262-6:2018 (unit/integration/embedded verification, back-to-back), IEC 61508-3:2010 (V-model, verification), ISO 13849-2 (валидация с fault-injection), NASA SWEHB 9.12/9.14 (margins, resource measurement), практика HIL-регрессии (OPAL-RT), trace-based test selection (ICST'16), MIL/SIL/PIL/HIL (MathWorks), ECUFuzz (ISSTA 2025), confirmation measures ISO 26262.
