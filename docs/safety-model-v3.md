# Safety-модель V3 (Safety & Health)

Статус: **все решения владельца приняты (Q1–Q8; D_accept = O3; Q5 = A; F2 — внешние ограждения + процесс, без интерлока); независимое экспертное ревью проведено — замечания F1–F10 учтены (F1–F6, F9 — решения владельца; F7, F8, F10 — правки записи); каталог — first-pass (ревью на G2); приёмка O3 условна на разрешение C1b в #48.** Финализация — при закрытии issue #45. Вход в logical item `Safety & Health` нормативного пакета (issue 8, gate G2) и в item `Quality Attributes & Budgets` (тикет #48).

Термины — канонические из `CONTEXT.md`. Исполнение — cooperative scheduler с bounded run-to-completion steps (issue 10), ports-and-adapters с единственной arbitration-воронкой в Safety Authority (#43).

## 1. Каталог опасностей

### 1.1 Схема записи

Каждая hazard-запись каталога имеет фиксированную схему (решение Q1 + поправка о residual-risk полях):

`HZ-ID` · `класс (operating context × interface)` · `причинная цепочка` · `вред` · `начальный риск (severity × likelihood/exposure, качественный, confidence)` · `детекция` · `controls (предотвращение/смягчение)` · `safe state` · `реакция (stop-профиль)` · `recovery` · `остаточный риск (качественный, confidence)` · `verification obligation` · `acceptance (статус + authority)`

Правило disposition: каждый V1-факт fault/warning получает решение «сохранить / изменить / исключить / неизвестно» (правило карты); wire-реестр fault/warning-кодов (коды, latch/timed-семантика) — собственность тикета «Специфицировать protocol V3» (#47), здесь фиксируются только доменные условия детекции. Начальный и остаточный риск — качественные (H/M/L), с confidence; численная классификация риска не заявляется.

### 1.2 Каталог (first pass, draft для item 5)

Каталог построен из V1-свидетельств (индекс, commit `a7f927c`, ветка `research/v1-system-evidence`) и walkthrough operating contexts (issue 2) × интерфейсы. Требует ревью при формировании пакета (G2).

| HZ | Класс (context × interface) | Причинная цепочка | Вред | Нач. риск | Детекция | Controls | Safe state | Реакция | Recovery | Ост. риск | Verification | Acceptance |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| HZ-01 | normal auto, manual × Sensing (ToF) | потеря направленной сенсорики в движении → возраст образца > T_fresh | столкновение с паллетой/стойкой; контакт с человеком | H (severity) × L (likelihood); confidence: low — нет полевых данных | freshness (T_fresh, каркас C1) | каденция ≤ T_sample_worst; T_fresh по C1b; INV-SENSING-FRESH | Stop | IMMEDIATE + fault (directional ToF) | qualified auto-clear (stationary + streak) | полный путь C1 — **unresolved (#48, F1)**; `v×T_fresh` ≈ 0.3–0.5 м при типовой скорости; bumper — физический backstop (**защита объекта/груза, F2**; человек — внешняя граница доступа) | proving #1; host: staleness-логика | owner (accepted O3 — T_fresh 300 ms; условно на C1b/#48) |
| HZ-02 | все motion-контексты × GPIO bumper, CAN | контакт с препятствием/человеком → bumper edge → ISR ring → latch | травма человека; повреждение шаттла/груза | H | bumper ISR (falling edges), crash counter | force-stop (min extended ID + выделенный mailbox, вне очередей); D_accept ограничивает энергию контакта | Stop (force-stop) | FORCE-STOP + latch + crash counter | explicit reset (persisted marker, Q5 A) | контакт на скорости, ограниченной D_accept; энергия контакта не измерена; человек — внешняя граница доступа (F2) | proving #3, #13; host: latch | owner |
| HZ-03 | все motion-контексты × CAN (100/101), приводы | отказ CAN-шины/драйвера → команды не доходят | неуправляемое движение | H (до мер; закрыт Q7.1 A) | CAN error states, отсутствие статусных RX (детали — Q7) | **per-device commissioning-тест fail-safe приводов (Q7.1 A)** + firmware-смягчение (error counters, bus-off recovery, stop при error passive) | Stop | FORCE-STOP (Q4-маппинг) | явный reset после проверки шины | **M — до фиксации ручного ре-теста в плане ТО, затем L** (F6; в firmware не контролируется) | proving #13; commissioning-процедура + периодический ручной ре-тест fail-safe (#50/#52) | owner (mitigated, Q7.1 A + F6) |
| HZ-04 | normal auto, manual × semantic contract, Actuator | ошибка/утечка состояния команды (V1: inverse-leak) → физически инвертированное движение | движение не туда → столкновение | M | commanded/observed разделение (#43), инварианты перехода | single-writer ownership, snapshot-чтение; host-тесты | Stop | stop при аномалии | reset | низкий (M-инварианты проверяемы) | host: property tests (admission, переходы) | owner |
| HZ-05 | normal auto, manual × Sensing | деградация сенсорики (шум/пропуски) → degraded health | снижение качества обнаружения | M | health state machines (V1: 16-sample окно, median 5, streaks) | состояние Degraded; движение — только при выполнении motion-инвариантов (свежесть + классификация); capability-ограничения — item 4 | Degraded | события + warning, ограничения по классам | квалифицированный recovery | движение в Degraded ограничено инвариантами | host: health state machine; proving #1 | owner |
| HZ-06 | все контексты × I2C (ToF, AS5600, BMS) | stuck SDA/SCL, BMS bus-busy → потеря наблюдений | потеря сенсорики (→ HZ-01), ложные данные | M | bus monitor (SDA/SCL states), failed vote всех ToF + AS5600 | адаптер: reinit, ≤16 SCL-импульсов, STOP, cooldown ≥5 s; Sensing: health state machine | Degraded/Stop | stop при влиянии на motion-сенсорику | квалифицированный (bus recovery) | окно без наблюдений ограничено | proving #14; host: recovery FSM | owner |
| HZ-07 | normal auto, manual, service × Actuator (лифтер), CAN 101/2405 | timeout/застревание лифтера, отказ концевика | падение паллеты, застревание, повреждение | H | концевики, timeout (V1 baseline 3800 ms), ток 2405 | ramp, timeout, INV-LIFTER-BOUNDS | Stop | CONTROLLED (покой) / IMMEDIATE (движение) + fault | явный reset после проверки | застревание требует человека | host: lifter FSM; proving | owner |
| HZ-08 | normal auto × Actuator, CAN 2405 | потеря удержания паллеты при подъёме/движении | падение паллеты, повреждение груза | M | ток лифтера 2405, концевики | ramp, bounded подъём, timeout | Stop | stop + fault | явный reset | падение на малой высоте (не измерено) | proving: lifter; host | owner |
| HZ-09 | normal auto, manual × Sensing (одометрия), Actuator | no-progress при commanded motion → stall | застревание, перегрев привода, повреждение | M | no-progress окно (Safety Authority, #43; V1: blink_Work, move timeout) | INV-STALL-NO-PROGRESS | Stop | stop-intent + stall fault | явный reset | застревание требует человека | proving #8; host: no-progress логика | owner |
| HZ-10 | все контексты × BMS, Actuator | SOC ≤ minBatt → потеря энергии в движении | застревание без энергии, отказ | M | fresh BMS sample, SOC ≤ порог | INV-LOW-BATTERY: авторизованная bounded safety-реакция | Stop (после реакции) | SAFETY_MOTION (лифтер вниз, bounded return) → CONTROLLED + fault | qualified-clear: новый fresh sample > порог | return ограничен границей реакции | host: reaction FSM; proving #11 | owner |
| HZ-11 | normal auto × паллетные сенсоры, ToF | ошибка позиционирования (канал полон, паллета не найдена, размер) | повреждение паллеты/шаттла | M | паллетные сенсоры, ToF (V1-таксономия warning) | per-operation алгоритмы (#30–42), классификация ожидаемой близости | Stop/Degraded | warning + stop по алгоритму | продолжение операции после recovery | повреждение груза при ошибке | algorithmic docs; host | owner |
| HZ-12 | attended manual × radio/display, Manual Session | потеря lease/связи при ручном управлении | неуправляемое движение | H | lease timeout (единый lease V3), link health | INV-LEASE-STOP: expiry → stop-intent; per-operation policy при link loss (#42: continue/controlled stop) | Stop | CONTROLLED (bounded) | повторная сессия | bounded stop гарантирован | proving #6; host: lease FSM | owner |
| HZ-13 | commissioning, service × Config & Profile | невалидная/отсутствующая конфигурация, непровиженный шаттл | движение с неверной геометрией | M | валидация профилей, unprovisioned ID 0 | motion запрещено до provisioning; валидация профилей 800/1000/1200 | Degraded (motion off) | отказ admission | provisioning flow | операторская ошибка | host: валидация; #50 | owner |
| HZ-14 | все контексты × Persistence (flash) | повреждение журнала конфигурации, power-cut при save | потеря конфигурации/калибровок | M | CRC16 журнала, маркеры | journal-механика, атомарность; quiescence (C6); восстановление калибровок (#43) | Degraded (defaults, unprovisioned) | fault/warning, defaults | восстановление из журнала | потеря неподтверждённых настроек | proving #3, #11; host | owner |
| HZ-15 | update × Update Authority, Persistence | power-cut/битый образ при update | «кирпич», нерабочая прошивка | H | staged-маркер, проверка образа | staged + rollback, recovery mode; INV-UPDATE-STATIONARY | Degraded (recovery mode) | отказ активации, откат | восстановление из recovery | окно неработоспособности | proving #11 (power-cut mid-update) | owner |
| HZ-16 | все контексты × ADC, BMS | перегрев MCU/батареи | повреждение, отказ | L (confidence: low) | ATEMP (V1), BMS-температура (детали неизвестны) | мониторинг, low-battery реакции | Degraded | warning → stop по порогам | охлаждение → recovery | не определено | host; требует решения по порогам | owner (open-obligation: #48/#50, до G2) |
| HZ-17 | все контексты × BMS (RS-485) | потеря наблюдения BMS → неизвестный SOC | глубокий разряд, застревание в проходе | M (confidence: low — нет полевых данных) | BMS staleness после T_bms_stale (V1: только warning — BmsDdA5.hpp:74-75, Cntrl_V2.ino:7702-7706) | warning + событие + лог; работа продолжается — **явный acceptance владельца (F9)** | Degraded (информационно) | warning, продолжение рабочего процесса | восстановление BMS-связи | глубокий разряд — принятый риск (покрытие внешним BMS-контроллером не подтверждено) | host: staleness-логика; T_bms_stale — #48 | owner (accepted, F9) |

### 1.3 Disposition V1-таксономии (evidence)

| V1-бит | Disposition | Основание |
|---|---|---|
| FAULT_TOF_* (4), FAULT_AS5600 | keep | HZ-01/HZ-05, qualified recovery (Q5) |
| FAULT_BUMPER_* (4) | keep | HZ-02, explicit reset + persisted marker (Q5 A) |
| FAULT_LIFTER_TIMEOUT | keep | HZ-07 |
| FAULT_MOTOR_STALL | keep | HZ-09, explicit reset |
| FAULT_LOW_BATTERY | change | HZ-10: qualified-clear по fresh sample (не human-ack) |
| FAULT_MOVE_TIMEOUT | keep | HZ-09, explicit reset |
| WARN_PALLET_NOT_FOUND, WARN_CHANNEL_FULL, WARN_NOT_IN_CHANNEL, WARN_PALLET_SIZE, WARN_END_OF_CHANNEL | keep | HZ-11; достижимость — по #44 (WARN_CHANNEL_FULL только из compact/long/demo) |
| WARN_MANUAL_TIMEOUT | keep | HZ-12 (единый lease V3 вместо 60 s/3 s асимметрии) |
| WARN_I2C_RECOVERY | keep | HZ-06 |
| WARN_OBSTACLE | change | HZ-01/HZ-11: классификация ожидаемой/неожиданной близости (#43), stop при неожиданной |
| sensorOff bypass | drop | мёртвый недостижимый bypass (флаг инициализирован 0, нигде не присваивается 1 — Cntrl_V2.ino:580, потребители 3537–3560); тип исключается из V3; любой будущий service-режимный bypass сенсорики — только через явную авторизацию Safety Authority (#50) |

### 1.4 Residual-risk authority и evidence (Q8 — решено)

Authority остаточных рисков — **владелец, через G2 gate** (issue 8: owner approval принимает residual risks). Форма — Governance risk-acceptance записи: статусы `accepted` / `mitigated` / `open-obligation` (с датой) per hazard в поле `acceptance` схемы. Unknown-статусы на G2 не допускаются: каждый hazard закрыт обязательством (с владельцем) или явным acceptance. Verification obligations — записи класса Evidence (issue 8), обязательны до G2 или явно отклонены с основанием. Спец-случаи: HZ-03 `mitigated` (Q7.1 A); HZ-16 `open-obligation` (пороги — #48/#50); HZ-01 `accepted` при **O3** (условно, до разрешения вилки C1b в #48 — F1 ревью).

## 2. Системная safety/health-модель состояний (Q2 + поправки)

Состояния — health-ось Safety Authority (issue 2: `Error` — health condition, не глобальный mode):

```
Initializing → Ready ↔ Degraded → Fault
```

- **Initializing**: стартап, движение запрещено; grace-окно (V1 baseline 1 s) + requalification health-gates; выход — только по готовности (INV-STARTUP-GATE). Reset-cause-aware: при pending explicit-reset маркере из Backup SRAM (Q5 A) — переход в `Fault`, минуя Ready.
- **Ready**: движение разрешено; все health-gates проходят (свежесть, fault-маска пуста, watchdog здоров).
- **Degraded**: движение ограничено по capability-классам (конкретные ограничения — item 4 `Capability & Operation Contracts`; здесь: движение только при выполнении motion-инвариантов INV-SENSING-FRESH и INV-OBSTACLE-CLASSIFY). Вход — degraded-условия (HZ-05/06/13/14/16); выход — снятие условия. Событие + warning, никогда молча. **F5 (ревью, решение владельца):** в Degraded действует **потолок скорости ≤ 1.0 м/с** (утверждено владельцем); временной bound `T_deg` → Fault/stop — рекомендация ревью, **не утверждена**, вопрос для #48; ограничение не откладывается в item 4.
- **Fault**: latched fault; движение запрещено для operation/manual intents; воронка допускает только safety-intents, авторизованные Safety Authority (bounded, с метаданными границ), stop/force-stop и recovery-пути (Q2 поправка: единый Fault + правило допуска; fault-классы ProtectiveStop/EvacuationRequired отклонены — V1 low-battery исполняет движение до latch, эвакуация — динамическая авторизация, не свойство класса).

Переходы (Q5 A): `Ready/Degraded → Fault` — любой latched fault; `Fault → Degraded/Ready` — только после qualified stationary recovery + (для crash-класса) явного acknowledgment (reset-error); power-cycle не считается acknowledgment; low-battery — qualified-clear по fresh sample.

## 3. Safety-инварианты (Q3)

Форма: `INV-<имя>: proposition (над snapshot'ами) ⇒ требование` + deadline-параметр (значения — каркас раздела 6 / #48) + verification obligation. Все инварианты проверяемы на host.

| Инвариант | Правило | Источник |
|---|---|---|
| INV-SENSING-FRESH | commanded motion ⇒ направленная сенсорика свежа (до старта и в движении); `sensorOff` исключён | V1 keep; drop sensorOff |
| INV-FAULT-ADMISSION | latched fault ⇒ внешние operation/manual intents отклонены; только авторизованные safety/stop/recovery | V1 keep (формализация) |
| INV-BUMPER-FORCE-STOP | bumper ⇒ force-stop в пределах дедлайна + latch + crash counter | V1 keep |
| INV-LEASE-STOP | manual без свежего hold-to-run ⇒ stop-intent в пределах T_lease | V1 keep (единый lease) |
| INV-LIFTER-BOUNDS | лифтер: концевики ⇒ немедленный стоп; timeout ⇒ fault | V1 keep |
| INV-STALL-NO-PROGRESS | commanded motion ∧ ¬progress(окно) ⇒ stop-intent + stall fault | V1 keep (владелец #43) |
| INV-OBSTACLE-CLASSIFY | неожиданная близость ⇒ stop; ожидаемая (метаданные точек останова) — только классификация; защита не отключается молча | V1/#43 keep |
| INV-QUIESCENCE-SAVE | save/erase окно ⇒ подтверждённая stationary ∧ окно < safety-дедлайна ∧ < watchdog | V1/#43 keep |
| INV-LOW-BATTERY | fresh BMS SOC ≤ порог ⇒ авторизованная bounded safety-реакция (лифтер вниз, return, fault) | V1 keep (через воронку) |
| INV-EVACUATION-BOUNDED | evacuation intent ⇒ явная граница + авторизация Safety Authority + bounded метаданные | V3 new (#43) |
| INV-UPDATE-STATIONARY | update ⇒ stationary + quiescence + авторизация Update Authority | V1 keep (расширен) |
| INV-WATCHDOG-ARMED | reload на границе каждого bounded шага; health-агрегация в Safety Authority | V1/#43 keep |
| INV-FORCE-STOP-CHANNEL | force-stop вне очередей, всегда транслируем (min ID + выделенный mailbox) | #43 keep |
| INV-STARTUP-GATE | движение ⇒ Ready-статус Safety (после grace + requalification + reconciliation reset-cause; механика маркера — Q5 A) | V1/#43 keep (Q5 A) |
| INV-BRAKE-VALIDITY | тормозная способность проверена: на commissioning + ручные проверки по плану ТО (внешний контроль чеклистами, в firmware не регистрируется); D_brake — обслуживаемый параметр, bound дрейфа = сервисный интервал (#48) | F3 ревью, решение владельца |
| INV-CAN-FAILSAFE | отказ CAN-шины ⇒ stop-intent + fault; безопасность не зависит только от CAN TX; fail-safe приводов верифицирован per-device commissioning-тестом (Q7.1 A) | Q7.1 A (решено) |

## 4. Precedence и stop-профили (Q4)

Двухуровневая модель:

- **Уровень 1 — force-stop вне воронки**: прямой путь Safety Authority → CAN (min extended ID + выделенный mailbox, INV-FORCE-STOP-CHANNEL). Триггеры: bumper/столкновение (HZ-02), потеря CAN (HZ-03, детали Q7).
- **Уровень 2 — воронка** с тотальным порядком `SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT`; auto и manual — один класс (взаимное исключение — admission, issue 2); замена активного intent — только на границе bounded шага; stop-intents никогда не отклоняются.

Три stop-профиля: `CONTROLLED` (ramp, bounded rate), `IMMEDIATE` (нулевой кадр в следующую эмиссию), `FORCE-STOP` (extended min-ID кадр + нулевой кадр).

| Триггер | Профиль | Источник |
|---|---|---|
| Штатное завершение операции, внешняя stop-команда, end-of-channel | CONTROLLED | V1 keep |
| Manual lease expiry (единый lease V3) | CONTROLLED (bounded) | V1 keep, асимметрия display/radio снята |
| Fault в покое (lifter timeout, AS5600, I2C recovery, move timeout в покое) | CONTROLLED | V1 keep |
| Fault при активном движении (ToF fault, AS5600 fault, motor stall, move timeout) | IMMEDIATE | V1 keep |
| Bumper / столкновение | FORCE-STOP + latch + crash counter | V1 keep |
| Obstacle (неожиданная близость) | IMMEDIATE (детали Q7) | V1/#43 |
| Потеря CAN-шины | FORCE-STOP (детали Q7) | Q7 |
| Low-battery (fresh SOC ≤ порог) | SAFETY_MOTION (bounded return) → CONTROLLED + fault | V1 keep |
| Эвакуация (запрос/авто) | SAFETY_MOTION: bounded цель (начало канала, #42), авторизация Safety Authority, доступна и в Fault | #42/#43 keep |

## 5. Recovery и auto-clear (Q5 A)

- **Auto-clear eligible** (квалификация + stationary + отсутствие motion intent): sensing/bus faults — ToF, AS5600, I2C bus (HZ-01/05/06). Квалификация — V1-базелайн: sustained fresh/usable streak; окна и пороги — #48. Владелец механики — адаптеры, health state machine — Sensing.
- **Qualified-clear**: low battery — новый fresh BMS sample с SOC > порога (HZ-10).
- **Explicit reset (crash-класс)**: bumper/столкновение, motor stall, move timeout (HZ-02/09). Механика — **решено (Q5 A): persisted marker** в Backup SRAM (CRC, владелец Persistence adapter — #43) при latch; снимается только явным reset-error после реквалификации (V1-базелайн: recovery bus + проверка датчиков; непроверенное восстановление → повторный latch); **power-cycle ≠ acknowledgment** — стартап при любом reset cause с маркером → `Fault` (motion inhibited, сверка crash counter delta + breadcrumbs). Вариант B (power-cycle = acknowledgment, V1-стиль) отклонён владельцем. Warnings — timed auto-clear (V1 keep).
- **Restart**: watchdog reset / HardFault → полный стартап `Initializing → Ready/Degraded` после grace + requalification + reconciliation reset-cause (без маркера); breadcrumbs и reset cause сохраняются (Backup SRAM). Latched `Fault` никогда не рестартует сам (анти-осцилляция).
- **Radio recovery**: V1-базелайн (только stationary+idle, backoff 5/30/120/600 s, silence probe 15 s, audit 300 s), bounded async контрактом (не блокирует шаг — фикс V1-дефекта); владелец механики — radio transport adapter.
- **Fault-recovery movement**: отдельная bounded capability с явной авторизацией (ручная jog-сессия: ограничения скорости/дистанции/направления, issue 2), исполняется как авторизованный safety-intent через воронку; точный контракт — item 4.

## 6. Численные safety-дедлайны: каркас (Q6 v2)

Формулы hazard-производные; **числа звеньев не фиксируются здесь** — measurement-resolved в #48 (поправка acceptance #45, решение владельца: измерения на реальном шаттле в процессе переноса функционала).

- **C1 (слепое движение):** `v_max × (T_fresh + T_check_jitter + T_arb + T_emit) + D_brake(v_max, load_max, grade_worst) ≤ D_accept`
  - `T_fresh` — порог свежести (возраст последнего образца), входит целиком: от последнего валидного образца до обнаружения staleness;
  - `T_check_jitter` — worst-case интервал от пересечения возраста > T_fresh до проверки, которая это заметит (≤ 1 bounded шаг / control tick, INV-SENSING-FRESH на каждой границе шага);
  - `T_arb` — ≤ 1 bounded шаг (кооператив);
  - `T_emit` — worst-case до ближайшего TX-слота;
  - `D_brake(v, load, grade)` — тормозная дистанция как кривая (отклик привода + ramp + механика; измерение proving #1).
- **C1a (нормальная выборка):** `T_fresh > T_sample_worst + margin`, `T_sample_worst = T_round_robin + T_pauses` — живая сенсорика держит возраст образца ниже порога (иначе ложные staleness-стопы).
- **C1b (вилка осуществимости):** `T_sample_worst + margin < T_fresh ≤ (D_accept − D_brake(v_max)) / v_max − T_check_jitter − T_arb − T_emit`. Пустой интервал при v_max ⇒ снижение maxSpeed или пересмотр D_accept (решение #48 на измерениях).
- **C2 (детекция):** `T_detect = T_round_robin(N, slot) + T_pauses_worst` — вход C1a.
- **C3 (цепочка после обнаружения):** `T_eso = T_check_jitter + T_arb + T_emit`.
- **C4 (force-stop):** `T_fs = T_isr + T_step + T_mailbox`; структурный приоритет решён (#43), численное значение — измерение proving #3/#13.
- **C5 (lease):** `T_lease_stop = T_step + T_ramp(v, profile)`.
- **C6 (quiescence):** `W_flash < min(safety-дедлайн, watchdog)`; измеренный `W_flash` > допустимой задержки реакции ⇒ RAM-exec (развилка #43). Watchdog окно = `max(шаг, flash, ISR) + margin` (#43).

**Критерий владельца — РЕШЕНО: O3, узкая запись (F1 ревью).** Решение владельца: `T_fresh = 300 ms` фиксированный (V1 keep). **Полная дистанция остановки (левая часть C1: цепочка + D_brake) НЕ зафиксирована** — члены цепочки (T_check_jitter, T_arb, T_emit) и D_brake неизмерены; V1-оценка 0.3–0.5 м покрывает только член `v × T_fresh`. Остаётся **unresolved под C1**: #48 выводит полный D_accept (вилка C1b: снижение v_max / T_fresh / обоснованное расширение) **до G2**; приёмка O3 условна на это. **Порт-предпочтение владельца:** минимальное отклонение от V1-кода (существующие конфигурационные лимиты maxSpeed, 300 ms) — для простоты портирования. O1 (100 мм) и O2 (200–300 мм) отклонены. Скорость-масштабирование не применяется (V1 фиксированный порог).

### 6.1 Закрытие Unknown из #43 (Q7.1–Q7.4, решения владельца)

- **CAN fail-safe (HZ-03, Q7.1 v2) — РЕШЕНО: A (per-device commissioning-тест).** Одноразовый наблюдаемый тест каждого привода/шаттла перед входом в Ready (новый шаттл, после замены привода): (а) прекращение команд → наблюдаемое timeout/brake поведение; непрошедший привод в Ready не входит. Firmware-смягчения (CAN error counters, bus-off recovery, stop при error passive) идут в разработке без железа. Процедура — #50/#52, доказательство — proving #13. Отклонены: B (документальное подтверждение vendor) и C (движение без верификации — acceptance H). **F4 (ревью):** тест расширен на (б) **проверку force-stop кадра (мин-ID, выделенный mailbox): остановка привода кадром за `T_fs`** — реализуемость и реальная необходимость кадра открыты к проверке (поддержка приводами); при неподдержке C4 переякоривается на verified drive-timeout (timeout < дедлайна с учётом 4 s flash-окна). Flash-окно (деферral ≤ 4 s) — принятый риск владельца, изменений не требуется.
- **Quiescence (Q7.2, поправка по datasheet-фактам).** (а) движение подтверждённо остановлено по snapshot'ам Actuator/Safety; (б) **bumper-события в окне erase — решение владельца (Q7.2)**: первое ребро латчится (EXTI/NVIC pending) и обрабатывается после окна (deferred ≤ 4 s); повторные рёбра в окне схлопываются — crash counter может недосчитать (принятый остаточный риск); force-stop эмиссия отложена до конца окна; принято, т.к. окно quiescent (движение остановлено, bumper-функция — остановка движения); post-window обработка latch — пункт proving #3; (в) `W_flash < watchdog`; (г) `W_flash ≤ T_fs_target` (число — #48), иначе RAM-exec (развилка #43).
- **Stall-детекция (Q7.3).** No-progress = `commanded motion ∧ |Δposition| < T_np` за окно `W_np(v)` (скорость-масштабируемое; числа — #48). Классификация: **stall** (нет прогресса, нет сигнала препятствия) vs **obstacle** (неожиданная близость). Реакция — IMMEDIATE (Q4-маппинг) + stall fault (explicit reset + persisted marker, Q5 A). V1 div-by-zero (`2000000/maxSpeed`) закрыт валидацией maxSpeed на admission (#50).
- **Obstacle-классификация (Q7.4).** Safety Authority сравнивает snapshot'ы Sensing с **метаданными ожидаемых точек останова** (операция предоставляет, #43); неожиданная близость → **obstacle** → IMMEDIATE (Q4); ожидаемая → штатный поток операции. **Отсутствие/неоднозначность метаданных → unexpected (fail-safe default)**; защита не отключается молча (INV-OBSTACLE-CLASSIFY). Пороги — из геометрии профилей (800/1000/1200, offsets V1) + измерение (#48).

## 7. Bounds и measurement-resolved параметры

### 7.1 Evidence-backed bounds (datasheet/core-derived, primary sources)

Источники: ST DS8626 Rev 12 (March 2026) Table 40 (§6.3.12), RM0090 Rev 22 (§3.6, §32, §21, Table 107), Arduino_Core_STM32 @ bb4b804b (IWatchdog). Верифицировано субагентом-исследователем (первичные документы, не по памяти).

| Величина | Значение (worst case) | Источник | Max/typ | Confidence |
|---|---|---|---|---|
| Erase 128 KB сектора (sector 7, V1-журнал) | **4 s** (PSIZE=x8); 2.6 s (x16); **2 s** (x32, 2.7–3.6 V) | DS8626 Table 40 `tERASE128KB` | max | primary |
| Программирование 512 B (128 слов × 100 µs) | **12.8 ms** | DS8626 Table 40 `tPROG` (100 µs/слово max) | max, derived (арифметика) | primary + derived |
| Fetch кода из flash во время erase/program | **stalled** (single-bank F405: весь flash заблокирован; ISR с вектором/кодом во flash отложен до конца операции; NVIC держит pending) | RM0090 §3.6 p.84 | — | primary (+ SRAM-часть — inference) |
| bxCAN арбитраж | lowest ID выигрывает и mailbox-арбитраж, и шину; ABRQ (CAN_TSR) — аппаратный abort pending TX; TIxR write-protected при pending | RM0090 §32.7 pp.1077, 1094–1095, 1100 | — | primary |
| Watchdog API stm32duino | `IWatchdog.begin(timeout)` — **единицы = микросекунды**; V1 `Watchdog.begin(10000000)` = **10 s**; max 32 760 000 µs ≈ 32.76 s | IWatchdog.h:41, IWatchdog.cpp:36; пример IWDG_Button.ino | — | primary |
| LSI (IWDG clock) | 17 / 32 / 47 kHz (min/typ/max) → worst-case аппаратный timeout 22.3…61.7 s; бюджетировать на 17 kHz (самый длинный timeout) | DS8626 Table 35; RM0090 Table 107 | — | primary + derived |

Следствия для модели:

- **C6 (quiescence):** datasheet worst case `W_flash ≤ 4 s` — проектировать против этого bound (не измерения); измерение на PCB решает RAM-exec vs принятие окна (#43 развилка). Следствия: (а) watchdog окно = max(шаг, flash, ISR) + margin ⇒ ≥ 4 s + margin — V1 с окном 10 s консистентен (Unknown «единица аргумента watchdog» закрыт: µs, 10 s); (б) **bumper-события в окне erase — решение владельца (Q7.2)**: первое ребро латчится и обрабатывается после окна (deferred ≤ 4 s); повторные рёбра схлопываются (crash counter недосчёт, принятый риск); force-stop эмиссия отложена до конца окна — окно quiescent, движение остановлено; post-window обработка latch — в proving #3; (в) force-stop эмиссия откладывается до конца окна (≤ 4 s); **покрытие интервала fail-safe приводов не утверждается априори** — поведение привода в окне без команд (timeout/brake vs окно) проверяется commissioning-тестом Q7.1 A (F4).
- **CAN force-stop:** hardware-арбитраж lowest-ID-wins подтверждает дизайн #43 (min extended ID + выделенный mailbox); ABRQ доступен для принудительной замены pending TX (деталь контракта адаптера, #47).
- **ISR во flash-окне:** deferral ISR до конца окна (≤ 4 s) — подтверждает obligation #3 proving slice; **SRAM-ISR не требуется — решение владельца (Q7.2)**: первое ребро латчится (EXTI/NVIC pending) и обрабатывается после окна; SRAM-вариант остаётся опцией при необходимости (решение по измерению #54).

### 7.2 Measurement-resolved параметры (обязательства измерения)

Закрываются в #48 от измерений (proving slice #54 + тесты на реальном шаттле в процессе переноса функционала — решение владельца):

`T_sample_worst` (планировщик, BMS/radio паузы) · `T_check_jitter` · `T_arb` (длительность bounded шага) · `T_emit` (TX-слот) · `T_isr` (вход ISR, включая окно flash) · `W_flash` (erase/program на этой PCB) · `D_brake(v, load, grade)` · `T_ramp` · margin watchdog. Каждое число получает источник (hazard-derived, code-derived, datasheet-derived или измерение) и validation obligation; аналитические оценки не называются measured (acceptance #48).

## 8. Assumptions и Unknowns

- **F2 — РЕШЕНО (владелец): граница доступа в проходы.** Внешние физические ограждения проходов существуют; доступ персонала контролируется процессом/WMS (персонал в проходе — только при остановленном шаттле); изменения на шаттле не требуются — **электрический интерлок/ESPE на шаттле отсутствует**. Следствие: ToF/bumper-цепочка — **защита объекта/груза**; защита человека — внешняя граница (ограждения + процесс), не интерлокирована с шаттлом; **residual HZ-01/HZ-02 для контакта с человеком — процесс-зависимый, выше, чем при интерлоке** (записано явно; усиление — только внешней установкой, вне scope прошивки).
- **Fact:** erase/program flash на single-bank STM32F405 останавливает выборку кода из flash (RM0090 §3.6); ISR с вектором/кодом во flash откладывается до конца операции; SRAM-резидентные ISR исполняются (inference из формулировки stall). Max erase 128 KB = 4 s (DS8626 Table 40, x8).
- **Fact:** `Watchdog.begin(10000000)` в V1 = `IWatchdog.begin(10000000)` = **10 s** (единицы — микросекунды). Unknown «единица аргумента watchdog API» закрыт.
- **Assumption:** типичная скорость шаттла 1.0–1.7 м/с (из оценки индекса V1: 0.3–0.5 м за 300 ms); точные профили — #48.
- **Unknown:** численные пороги и детекция тепловых реакций (HZ-16) — `open-obligation`, владелец #48/#50, до G2.
- **Unknown:** фактические производственные значения (калибровки, профили, пороги) на эксплуатируемых устройствах.
- **Unknown:** достижимость части V1-битов (FAULT_MOVE_TIMEOUT, WARN_END_OF_CHANNEL) — открыто в #44.
- **Confidence:** начальные/остаточные риски каталога — инженерный анализ без полевых данных; классификация не является измерением.

## 9. Ссылки

- Тикет «Разработать инженерную safety-модель V3» (#45) и resolution-комментарии.
- «Определить архитектурные границы и ownership foundation V3» (#43), `docs/software-architecture-boundaries-v3.md`.
- «Выбрать execution architecture V3 по Gates + evidence» (#10).
- «Определить system context и таксономию возможностей V3» (#2), `CONTEXT.md`.
- Системный индекс свидетельств V1 (commit `a7f927c`, ветка `research/v1-system-evidence`).
- «Определить структуру нормативного пакета и verification gates» (#8) — item 5, G2.
- Алгоритмическая документация (#16, #30–42) — per-operation доменные алгоритмы.
