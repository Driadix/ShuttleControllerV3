# Архитектура наблюдаемости V3 (Observability Architecture)

Статус: **утверждено владельцем (гриллинг Q1–Q3, тикет «Спроектировать архитектуру наблюдаемости V3» (#49 карты #1))**. Вход в logical item 8 (`Observability & Diagnostics`) нормативного пакета (issue 8, gate G4) и в architecture proving slice (тикет #54).

Термины — канонические из `CONTEXT.md`: Подписка, Транспортный профиль, Controller Epoch, Outcome операции, Платформенное окно. Исполнение — cooperative scheduler с bounded run-to-completion steps (issue 10), ports-and-adapters (#43), lifecycle-оси (#46), численные бюджеты — `docs/quality-attributes-and-budgets-v3.md` (#48).

## 1. Назначение и границы

Наблюдаемость V3 — это четыре bounded исходящих класса (`telemetry`, `events`, `logs`, `traces`), авторитетный query snapshot, реестры событий/счётчиков, retention/экспорт и диагностические бюджеты. Она закрывает восемь evidence-дефектов V1 (см. §12) и измеряет availability ≥ 99.5% (#48 Q8).

Вне границ: WMS/аналитика (потребители, реализуются вне прошивки), дисплей как отдельный профиль (дисплей — клиент `network_bridge`, CONTEXT.md), протокольные коды fault/warning (владелец #47 §16.4), калибровочные потоки (#50).

## 2. Классы и схема записей

### 2.1 Общий envelope (все записи всех классов и фрагменты snapshot)

| Поле | Тип | Обязательность | Семантика |
| --- | --- | --- | --- |
| `classId` | u8 (2 бита значимы) | да | класс на wire — explicit `queueClass` (#47 §8.2) |
| `controllerEpoch` | u32 | да | fencing boundary (#13); меняется на reboot |
| `monotonicTick` | u32 | да | ms от boot; **единственный авторитет порядка** (#43, #48 §9) |
| `seq` | u8 | да | сквозной per-class счётчик (rolling); детекция потерь (паттерн Sparkplug/OPC UA) |
| `wallTime` | u32 (epoch sec) | по классу | wall-время, только при валидности (см. §3) |
| `timeValidity` | 2 бита | по классу | `Unsynced` / `RTC-only` / `Synced` (см. §3) |

Стоимость: telemetry-запись ~9 Б, events/logs ~14 Б — укладывается в MTU 128 Б (#48).

### 2.2 telemetry — периодическое состояние

Периодическая запись операционного состояния; drop-oldest, приоритет свежести (#43).

Поля (после envelope): `opState` (текущая operation/состояние), `position` (мм), `speed`, `health` (Initializing/Ready/Degraded/Fault, #45), `faultMask` u16, `warningMask` u16 (реестр #47 §16.4), `batteryCharge`, `batteryVoltage_mV`, `palletCount`, `stateFlags` (lifterUp / motorStart / motorReverse / CHANNEL / inverse). Каденция — профильные дефолты и подписки (§9); bridge-дефолт 300 мс (V1 keep, sizing #48 §6).

Freshness-класс: wall-время не несёт (порядок — tick), авторитет состояния — snapshot (§2.6).

### 2.3 events — дискретные типизированные происшествия

Запись: envelope (+wall, +timeValidity) + `eventId` (u16, реестр §5) + `severity` (u8: `info`/`warning`/`error`/`fatal`) + bounded context (типизированные key-value поля: `operationId`, `faultCode`, `dropCounter`, `rejectCode`, `updateStage`…). Резерв ёмкости, drop-newest (#43).

События — единственный класс, где текстовая строка не обязательна и не заменяет коды: реестровые коды — норма (#47 §14, A6). Каждое drop/reject очереди — событие + счётчик (#43).

### 2.4 logs — человекочитаемая диагностика

Запись: envelope (+wall, +timeValidity) + `level` (u8: `DEBUG`/`INFO`/`WARN`/`ERROR`/`FATAL`) + `moduleId` (u8) + bounded text (≤ 80 Б после envelope — помещается в MTU 128 Б; длинные сообщения не chunk-атся, а обрезаются — фикс V1 chunk-сплита, где каждый кусок терялся независимо).

Уровни отображаются на RFC 5424 severity при экспорте на мосту. Резерв ёмкости, drop-newest (#43). Политика содержимого (A6): никаких секретов, ключей и полных payload'ов; пароль/ключевые материалы никогда не логируются.

Фильтрация: runtime-порог уровня (глобальный + per-module таблица, ≤ 16 модулей) — сообщения ниже порога отбрасываются **до форматирования** (паттерн DLT); порог — конфигурация через Service-класс (#50 — surface). DEBUG-сайты в production-сборке исключаются compile-time флагом (V1 `CONNECTION_LOGS=0`-паттерн, #51).

### 2.5 traces — захват (решение владельца Q1: C)

Два подвида, одна схема записи, поле `kind`:

- **kind=fault_capture** (production-контракт): на latch фолта Observability Producer собирает bounded запись: `triggerEventId` + `triggerTick` + хвост колец events/logs до фолта (до N записей) + фрагмент snapshot (commanded/observed, §2.6) + аппаратный контекст (reset cause; регистры HardFault — если есть). Также — по Service-команде захвата (полевая диагностика).
- **kind=dev_timeline** (compile-time флаг `V3_DEV_TIMELINE`, default off): per-tick переходы состояний, сводки TX/RX кадров; в production-сборках отсутствует.

Класс bounded, drop-oldest; reserved-флаг для fault-коррелированных записей (#47 §8.2). Персистентность — §8.

### 2.6 Query snapshot — авторитетная реконсиляция

Единый документ состояния: `version` (u32, monotonic +1 на изменение), `controllerEpoch`, `wallTime` + `timeValidity`, identity (firmware version, build id, hardware id, serial — A6), платформенное окно (#46), health (#45), provisioning-статус, сводка дерева операций, sensing (position, freshness), actuator commanded, battery, faultMask/warningMask, счётчики (§6), профиль 800/1000/1200.

Доставка — фрагментами ≤ MTU 128 Б (`Snapshot fragments`, #47 §7); version+epoch — fencing против устаревших snapshot'ов (паттерн AWS Shadow: старые версии отбрасываются). Snapshot отдаётся: по Query; на (re)subscribe (birth-паттерн Sparkplug); на детект пропуска `seq` (gap → re-sync).

## 3. Clock model

- **Monotonic** — единственный авторитет порядка и таймаутов (уже решено #43/#48): `monotonicTick` в каждом envelope.
- **Wall** — HAL RTC adapter; синхронизация SetWallClock (Service-класс, #47); NTP-время ставит внешний backend через мост. Скачок wall (NTP-коррекция) не влияет на monotonic (validation #54 #9).
- **Качество времени** (A6, RFC 5905-паттерн): `timeValidity` = `Synced` (SetWallClock получен и RTC считает от LSE) / `RTC-only` (RTC идёт, синхронизации в этой эпохе не было) / `Unsynced` (RTC не инициализирован). При `Unsynced` wall-время в записи отсутствует (не эмитировать «убедительное» время с мёртвой батареей — V1-дефект: молчаливый сброс RTC на 2023-01-01).
- Порядок записей клиентом: `(controllerEpoch, monotonicTick)`; логические часы (Lamport/vector) не нужны — единственный писатель (#49 research).

## 4. Ownership и потоки (acceptance #43: каждый поток bounded и имеет владельца)

| Поток | Владелец-писатель | Куда |
| --- | --- | --- |
| fault latch / clear, health-переходы, Degraded-вход (никогда молча, #45) | Safety Authority / Sensing | events |
| Outcome (terminal, typed) | Operation Runtime | events (family Outcome, #47 §7) |
| Admission rejects, drop/reject очередей, overload | Semantic Contract (rejects) / Observability Sink (drops) | events + счётчики |
| Переходы окон, boot/reset (reset cause, marker reconciliation) | execution core | events |
| Update этапы/исходы/rollback-причина | Update Authority + Persistence | events (+ durable journal §8) |
| Lease expiry | Manual Session (писатель expiry-события, #43) | events |
| Счётчики/статистика | **Observability Producer — единственный** (компоненты и адаптеры только эмитят события) | inline-инкременты в шаге |
| Очереди классов, TX-планирование, механическое enforcement | Observability Sink | — |
| Flash-журнал, Backup SRAM (markers, breadcrumbs, stats) | Persistence adapter (запись — по запросам Producer) | — |
| ISR (bumper edges) | только bounded ring GPIO adapter; классификация — вне ISR (#43 §3.2) | ring |

## 5. Реестр событий (таксономия и диапазоны ID)

Стабильные `eventId` (u16), диапазоны по категориям; назначение конкретных значений — item 6 elaboration (registry-контракт #47):

| Диапазон | Категория | Примеры |
| --- | --- | --- |
| 0x01xx | Fault/Warning | latch/clear по HZ-кодам (#45 §1.2), severity=error/fatal; warning set/expire |
| 0x02xx | Health | переходы Initializing/Ready/Degraded/Fault; T_deg истечение |
| 0x03xx | Платформенное окно | Boot→Serving, Serving→Update, Update→Serving/Recovery, Recovery→Serving/Boot |
| 0x04xx | Admission | каждый reject с `rejectCode` (реестр #47 §16.2), ResourceConflict, BusyRejected |
| 0x05xx | Queue/Overload | drop per-class, drop per-subscription, reject per-class |
| 0x06xx | Операции | Outcome terminal (family Outcome, коды #13), session open/close, lease expiry |
| 0x07xx | Update | этапы Staging/Applying/Committed/RolledBack/Failed/Aborted, rollback-причина |
| 0x08xx | Boot/Reset | boot-причина (reset cause категория + marker), watchdog reset, HardFault, epoch смена |

Каждый event: `eventId` + bounded context; никаких ad-hoc строк как единственного сигнала (#47 §14). Fault/warning wire-коды (битовые маски telemetry) остаются у #47; события ссылаются на них полем `faultCode`.

## 6. Счётчики и статистика (Observability Producer)

Типизированный каталог (`counterId` u16, реестр по образцу eventId; значения — в snapshot и stats-query). Disposition V1:

| Счётчик | Disposition | Примечание |
| --- | --- | --- |
| loadCounter, unloadCounter, compactCounter, liftUpCounter, liftDownCounter, lifetimePalletsDetected | **keep** (V1 `sramStats`) | lifetime |
| totalDist (мм) | **keep, fix cost** | V1: CRC всего пакета на каждый тик одометрии — в V3 инкремент в шаге, CRC при save (fix дефекта №4) |
| motorStallCount, lifterOverloadCount, lowBatteryEvents, crashCount | **keep** | crash-class: участие в trace-записи §8 |
| resetWatchdog/Software/Pin/Power/OtherCount, lastResetFlags | **keep** | категории RCC (#45/#48) |
| totalUptimeMinutes | **keep** | lifetime, ~1/мин |
| **new:** `uptime_s` (с boot) | add | паттерн sysUpTime (RFC 2863) |
| **new:** `lastBootCause` | add | категория reset cause + наличие persisted marker (#45 Q5 A) |
| **new:** per-HZ latch счётчики (ToF/I2C/AS5600/…, u16) | add | историчность фолтов (V1-дефект №6) |
| **new:** per-class drop/reject счётчики (5 классов × u16) + high-water | add | обязательство #43: «каждый drop/reject → счётчик»; evidence #54 #7/#10 |
| **new:** per-subscription drop счётчики | add | медленный потребитель наблюдаем (§9) |
| **new:** per-AdmissionRejectionCode счётчики (bounded таблица) | add | диагностика отказов admission |

Агрегация: компоненты эмитят события; Producer инкрементирует на границе bounded шага (фикс V1: `__disable_irq` + CRC на каждый bump). Персистентность — §8.

## 7. Availability (≥ 99.5%, #48 Q8)

- Измерение: `uptime_s` + `totalUptimeMinutes` + reset-cause гистограмма + `lastBootCause` + `timeValidity`. Блок ~24 Б внутри snapshot (не отдельный класс).
- SLO считает **gateway/мост** (deadman по событиям/heartbeat, STALE-семантика §9), контроллер даёт атрибуцию потерь (boot cause, reset histogram) — паттерн VDA 5050 connection-state + Sparkplug STALE.
- Контроллер не эмитирует «искусственный» heartbeat при отсутствии подписки (нет mandatory push без подписки, #47).

## 8. Retention и персистентность

### 8.1 RAM-кольца

Размеры — #48 §6 (telemetry 8, events 32, logs 32, traces 16 записей); политики переполнения — #43 §6; каждое переполнение — событие + счётчик.

### 8.2 Flash-журнал диагностики (решение владельца Q2: A)

- **Crash-класс trace-записи** (глубокий контекст): explicit-reset фолты (bumper/столкновение, stall, move-timeout), watchdog reset, HardFault, update `Failed`/rollback-причина, reboot-циклы. Запись: триггер + tick + wall (если валидно) + хвост breadcrumbs + фрагмент snapshot + аппаратный контекст + срез статистики.
- **Durable events** (компактный слой, требование A6): safe-state переходы, watchdog/reset loop-ы, этапы/исходы update, rollback-причина — короткие записи (eventId + tick + wall + 1–2 поля контекста).
- Авто-сбрасываемые фолты (ToF/AS5600/I2C, #45 §5) — **только RAM + счётчик** (не в flash).
- Бюджеты: crash-записи ≤ 10/сутки, durable events ≤ 50/сутки; при превышении — drop-newest + счётчик (правила events-класса #43). Формат: record-паттерн с seq + CRC16 и integrity-сканом при boot (паттерн wear-leveled record, Betaflight-концепция erase-фронта).
- Разметка секторов/секций журнала — flash-карта #50 (dependency; приложение ≤ 512 КБ, #48 §8). Запись — только по quiescence (C6, #45/#48), reload watchdog между flash-операциями; в Boot — отложенная запись после реконсиляции reset-cause.
- Wear: ≤ 60 записей/сутки ≈ 30 КБ/сутки → эрase-цикл сектора 128 КБ раз в ~4 дня; при 10k циклов на сектор и 2 секторах — десятилетия (расчёт-основание; измерение — #54).

### 8.3 Backup SRAM (fix V1-дефектов)

- **breadcrumb-кольцо** (несколько записей, fix V1 single record) + persisted marker + reset flags — **с CRC16** (fix V1: маркеры без CRC, :286-288).
- Persistent stats (§6) — как в V1 (BKPSRAM, владелец Persistence), **плюс периодическая flash-копия** (раз в сутки на quiescence или при save-событиях) — fix дефекта «разряд VBAT обнуляет lifetime-счётчики».

### 8.4 Экспорт

- **Pull**: Query snapshot, log tail, stats, trace-записи — по запросу (read-only диагностика доступна в Serving/Update/Recovery, любой health, #46 §8).
- **Push**: только через подписки (§9) и профильные дефолты. Bridge — потоки возможны; radio — только events + подписная telemetry (Q3: A).

## 9. Подписки

Модель (CONTEXT.md «Подписка», #47 §14): bounded-соглашение `(класс(ы), filter, minInterval, maxBytesPerTick)` на principal.

- **Публикация гейтится подпиской**: поток telemetry/streams существует только при ≥ 1 активной подписке или объявленной profile default capability. Профильные дефолты: bridge — telemetry 300 мс (V1 keep, sizing #48) + events всегда; radio — **events всегда, telemetry только по подписке** (Q3: A).
- **Caps**: bridge ≤ 8 concurrent подписок, radio ≤ 2; per-subscription `maxBytesPerTick` — доля линк-бюджета (§10). Медленный потребитель: переполнение подписки → drop-oldest внутри подписки + счётчик (очередь класса сохраняет свою политику).
- **LIVE/STALE**: присутствие — связь линка (bridge: TCP-сессия; radio: link health); gateway маркирует контроллер STALE после deadman-таймаута (контроллер не фабрикует heartbeat). При (re)connect — полный snapshot (birth); детект gap по `seq` → re-query snapshot (re-sync).
- **Lifetime**: подписка умирает с сессией/epoch; re-handshake (#13).
- **Manual-сессия** (radio): авто-подписка на telemetry, minInterval 1 s (оператору нужны позиция/скорость в jog; HZ-12 lease).

## 10. Диагностические бюджеты (per-link, в рамках #48)

Enforcement — Observability Sink: приоритет TX (1) Control/Service ответы и ACK, (2) events, (3) logs, (4) traces, (5) telemetry; каждый класс имеет per-tick cap; суммарный drain ≤ линк-бюджета/тик (#48 §7). Переполнение класса сверх cap → политика класса (drop-oldest/drop-newest) + счётчик.

| Линк | Класс | Дефолт/мин-interval | Per-tick cap | Приоритет |
| --- | --- | --- | --- | --- |
| bridge (230 Б/тик) | telemetry | 300 мс (мин 200 мс по подписке) | 128 Б (1 MTU) | 5 |
| bridge | events | нет интервала (bursty) | 128 Б | 2 |
| bridge | logs | нет интервала | 60 Б | 3 |
| bridge | traces | по триггеру | 128 Б | 4 |
| radio (57 Б/тик) | events | нет интервала | 57 Б (резерв) | 2 |
| radio | telemetry | только подписка, мин 1 s | 57 Б | 5 |
| radio | logs/traces | не пушатся (pull только) | 0 | — |

Бюджеты validated: #54 #7 (переполнения, high-water), #54 #12 (log-storm, неблокирующий TX), HIL #52 (radio-трафик).

## 11. Решения владельца (Q1–Q3)

| Вопрос | Решение |
| --- | --- |
| Q1 | Семантика traces: **C** — fault-захват как production-контракт + dev-timeline за compile-time флагом `V3_DEV_TIMELINE` |
| Q2 | Персистентность: **A** — crash-класс trace-записи в flash-журнал (≤ 10/сутки) + durable events (A6, ≤ 50/сутки); авто-сбрасываемые фолты — RAM + счётчик |
| Q3 | Radio-поверхность: **A** — events всегда (резерв), telemetry по подписке (manual-сессия авто-подписка 1 Гц), logs/traces не пушатся |

## 12. V1-дефекты, закрытые этим дизайном

1. Логи без кольца и потерь → bounded классы + политики переполнения + счётчики (§2, §8.1).
2. «Reliable»-лог блокировал loop до 250 мс → never-block TX, producer-budget (#43 §4, #54 #12).
3. Нет таймстампов → envelope с tick + wall + timeValidity (§2.1, §3).
4. BKPSRAM-статистика без flash-копии → периодическая flash-копия (§8.3).
5. Фолты без истории → per-HZ счётчики, события latch/clear, flash-журнал (§5, §6, §8.2).
6. Нет глобального log-бюджета → per-tick caps + приоритеты + счётчики (§10).
7. Асимметрия каналов → единая подписочная модель + профильные дефолты (§9).
8. Маркеры/причины без CRC → CRC16 в Backup SRAM-структурах (§8.3).

## 13. Validation obligations (→ #54 proving slice, #52 pyramid)

1. Переполнения очередей под load, счётчики, high-water (#54 #7 — повторный вход).
2. Неблокирующий TX при log-storm (#54 #12).
3. NTP-скачок не ломает monotonic (#54 #9).
4. Сборка fault-capture из колец корректна (host property test: latch → record содержит триггер + хвост + snapshot-фрагмент).
5. Персистентность crash-записи через reboot (target: запись → reset → чтение; HIL: fault-injection → журнал).
6. Integrity-скан журнала при boot (повреждённая запись ≠ потеря журнала; CRC-поведение) — host + target.
7. Caps подписок и gap re-sync (host: подписочная машина; fake линк).
8. Availability-счётчики: категории reset cause (target: программный reset → счётчик; watchdog → счётчик).
9. Суточный лимит записей журнала: переполнение → drop-newest + счётчик (host).
10. Radio: events-резерв не вытесняется telemetry-подпиской (host + HIL radio-трафик).

## 14. Ссылки

- Тикет «Спроектировать архитектуру наблюдаемости V3» (#49) — гриллинг Q1–Q3, research-подготовка (V1 evidence, industry references).
- #43 (границы/владельцы, классы, overload), #45 (safety/health, breadcrumbs), #46 (окна, restart-таблица), #47 (протокол: registry, subscriptions, snapshot), #48 (бюджеты), #13 (semantic contract), #50 (flash-карта, конфиг-surface).
- `docs/research/v3-engineering-sources.md` A6 — диагностический контракт.
- `CONTEXT.md` — канонические термины.
