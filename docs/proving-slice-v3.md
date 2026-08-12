# Architecture proving slice V3 (тикет #54)

Статус: **draft для тикета [#54](https://github.com/Driadix/ShuttleControllerV3/issues/54)**. Ветка: `proving/slice-scaffold`. Входы: issue 10 (выбор execution architecture), [#43](https://github.com/Driadix/ShuttleControllerV3/issues/43) §8 (15 validation obligations), [#48](https://github.com/Driadix/ShuttleControllerV3/issues/48) §11 (measurement obligations) и §2–8 (бюджеты), [#45](https://github.com/Driadix/ShuttleControllerV3/issues/45) (safety-дедлайны C1–C6, stop-профили), [#51](https://github.com/Driadix/ShuttleControllerV3/issues/51) (frozen toolchain, §12), [#52](https://github.com/Driadix/ShuttleControllerV3/issues/52) (метод O4, протокол observed-maxima).

Правило карты: proving slice - **pre-Destination evidence** (issue 10) и единственное исключение из «только планируем». Он не реализует capability functions и не является production-каркасом. Его результат - сравнительный отчёт по трём kernel variants и закрытие 15 validation obligations #43 измерением либо явным переводом в следующий шаг.

## 1. Цель и границы

**Цель**: эмпирически подтвердить или опровергнуть условный выбор cooperative scheduler с bounded run-to-completion steps (issue 10) на реальном target до реализации capability functions, сравнив его с hybrid и static RTOS на **одинаковом** harness: одинаковые synthetic loads, одинаковые fault cases, одинаковые бюджеты.

**Что НЕ входит** (scope-границы, правило карты):

- Реализация capability functions (движение, лифт, загрузка) и их алгоритмов (#30–42).
- Production-каркас (CI workflow, enforcement-скрипты, `.clang-tidy`, PR-шаблоны) - после Destination.
- Проектирование domain-компонентов V3 в production-качестве (#43 даёт границы, slice ставит только минимальные упрощённые прототипы для измерения execution-свойств).
- Изменение PCB, электроники, механики.
- Полевые измерения (D_brake, v_max_phys, ATEMP, availability) - implementation-карта (#52 §5.2).

**Критерий выхода** (acceptance #54):

1. Сравнительный отчёт по трём kernel variants с рекомендацией по execution architecture.
2. Failure-условия пересмотра issue 10 проверены явно (см. §9).
3. Все 15 obligations #43 закрыты измерением или явно переведены в следующие шаги (список переводов - обязательная секция отчёта).

## 2. Структура harness

Одно дерево исходников, три target env + один native env (#51 §12):

| Директория | Содержимое | Примечание |
| --- | --- | --- |
| `platform/` | execution cores (`execution_coop.cpp`, `execution_hybrid.cpp`, `execution_rtos.cpp`), точка входа `main.cpp`, monotonic tick, watchdog policy, board init | единственное место kernel-логики; выбирается build flag `-DV3_KERNEL=coop\|hybrid\|rtos` |
| `domain/` | минимальные прототипы: arbitration funnel, safety health (упрощённый Safety Authority), static queues трёх классов, порты | host-deterministic, без Arduino/RTOS include (enforcement #51 §5) |
| `adapters/` | синтетические/инструментированные: CAN, UART (display/radio/BMS), I2C, flash, logging sink | реализуют доменные порты; target-варианты на Arduino Core |
| `proving/` | harness: генераторы нагрузок, fault injectors, measurement recorder, workload metadata, сценарии obligations | временный код evidence; политика утилизации - §10 |
| `tests/` | host-тесты (`pio test -e native-coop/hybrid/rtos`): deterministic unit/property/fault | три native env одного дерева (интерпретация #51 §12, §13.3) |
| `tools/` | `check_includes.py` (include-lint, #51 §5.2) | минимальный набор для slice |

**Инвариант harness**: смена kernel variant меняет только `platform/` (исполнение) - harness, нагрузки, fault-сценарии, измерения и тесты идентичны для всех трёх variants. Единственная конфигурация, которая может отличаться: `-DV3_KERNEL` и env-scoped пакеты RTOS.

### 2.1 Порты (domain → adapters)

Каждый adapter имеет bounded asynchronous контракт (#43 §4, #48 §7): `start/event-or-poll/cancel` с deadline; без блокирующих ожиданий. Порты для slice:

- `port_can`: TX bounded (≤ 16 кадров/тик), RX-drain с бюджетом (≤ 64 кадров/тик), force-stop путь (min extended ID + выделенный mailbox, вне очередей).
- `port_uart(класс)`: byte budget за тик (display 230 B, radio 57 B, BMS 10 B); non-blocking TX (DMA или producer-budget).
- `port_i2c`: слот-расписание (1 ToF-чтение / 8 ms; AS5600 / 250 ms), bus-busy координация, recovery ≤ 16 SCL + cooldown ≥ 5 s.
- `port_flash`: журнал sector 7 (0x08060000, 128 KB, страницы 512 B), quiescence C6, W_flash ≤ 4 s.
- `port_observability`: очереди telemetry 8 / events 32 / logs 32 / traces 16 (drop-policy #48 §6), счётчики overload.
- `port_monotonic`: 1 ms tick, wrap-safe 64-bit; `port_watchdog`: IWDG reload политика (execution core).

### 2.2 Минимальный domain (slice-grade)

- **Arbitration funnel** (упрощение #43 §3.1): тотальный порядок `SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT`; stop-профили CONTROLLED/IMMEDIATE/FORCE-STOP (#45 §4); единственный текущий intent наружу.
- **Safety health** (упрощение #45 §2): состояния Initializing/Ready/Degraded/Fault, freshness-проверка (T_fresh 300 ms), T_deg-отсчёт, latch-семантика fault-маски; без полного hazard-каталога.
- **Static queues**: Control 18 (16+2 резерв) / Service 8 / Update 4 + исходящие 8/32/32/16 (#48 §6); overload: reject на admission / drop-oldest / drop-newest; каждый drop/reject ⇒ счётчик.
- **Single-writer ownership**: один писатель на каждый mutable ресурс; ISR пишет только в свой bounded ring; кросс-доступ - read-only snapshot'ы.

Эти прототипы НЕ являются производственным дизайном компонентов #43: их цель - сделать execution-свойства (deadlines, boundedness, overload, starvation) измеримыми.

## 3. Три kernel variants

### 3.1 Cooperative (условно нормативный, issue 10)

- Один scheduling domain, один основной stack, run-to-completion bounded steps.
- `T_step = 10 ms` (#48 §4): каждый domain-шаг и adapter-шаг ≤ 10 ms; flash-окно - единственное исключение (quiescent, ≤ 4 s).
- Watchdog reload на каждой границе bounded шага + в idle-loop (#43 §4).
- ISR: только bounded ring capture (bumper edges), без policy (#43 §3.2).
- Scheduler: очередь bounded steps с дедлайнами; overrun детектируется и записывается как evidence.

### 3.2 Hybrid (fallback-кандидат)

- Определение issue 10: «изолированный critical execution path с cooperative/event-driven orchestration».
- Slice-минимум: cooperative core (как 3.1) + **прерываемый critical path**: bumper edge (ISR, приоритет 0) эмитит min-ID force-stop кадр **синхронно из ISR-контекста** через зарегистрированный ISR-safe emitter (bxCAN mailbox write, RM0090 §32.7) - преемпшн любого исполняемого шага (T_fs = T_isr + T_mailbox, C4); остальная логика идентична 3.1.
- Host-нога: `force_stop_isr()` вызывается fault-инжектором синхронно в точке вызова (симуляция преемпшна; реальная преемпшн-латентность - target evidence). Q7.2-collapse повторных фронтов применяется к cooperative deferral-пути (окно erase), не к hybrid.

### 3.3 Static RTOS

- Кандидат - FreeRTOS (решение по research RtosVariant, §13.1): де-факто индустриальный стандарт для статических RTOS (Amazon, огромная экосистема верификации); ststm32@17.4.0 НЕ бандлит FreeRTOS-фреймворк, но STM32duino core 2.7.1 готов к нему: `premain()` ставит `NVIC_PRIORITYGROUP_4` «Required by FreeRTOS», `SrcWrapper/clock.c` имеет weak-хук `osSystickHandler()` для SysTick-тика RTOS (факт, research 2026-08-12).
- Пакет и точный пин - **HITL-решение** (§13.1, briefing владельцу): официальный FreeRTOS-Kernel в PlatformIO-реестре отсутствует; in-registry кандидаты: `mincrmatt12/STM32Cube Middleware-FreeRTOS@10.3.1+f4-1.26.1` (ST-патченное ядро для F4-HAL, максимум индустриального соответствия) vs `linlin-study/FreeRTOS-Kernel@10.4.4-1` (plain kernel). Вне-registry (git-пин) запрещён политикой #51 §5.4.
- Конфигурация: статические задачи (safety / control+sensing / transport / observability), `configSUPPORT_DYNAMIC_ALLOCATION=0`, bounded IPC через статические очереди, SysTick-тик через `osSystickHandler()`, ISR nesting по NVIC 4-бит группе.
- Host-нога: официального host-порта FreeRTOS для этой конфигурации нет в реестре; host-лега RTOS-variant покрывает kernel-независимую логику (арбитраж, health, очереди), а исполнение - target. Это фиксируется в отчёте как ограничение сравнения.

## 4. Synthetic loads (обязательные, issue 10)

Каждый генератор параметризован интенсивностью; workload-матрица фиксируется для каждого прогона (#52 §6.3):

| Load ID | Генератор | Интенсивность | Бюджет-таргет |
| --- | --- | --- | --- |
| L1 CAN flood | RX-поток > 64 кадров/тик | 500 kbit/s ⇒ ~38 кадров/тик nominal; flood ×2–4 | #13: control-plane жив, T_fs не нарушен |
| L2 TX full | полные TX буферы CAN/UART | fill до отказа + подпитка | #12: ни один шаг > T_step |
| L3 UART flood | continuous/malformed RX на display/radio | > 230 B/тик display, > 57 B/тик radio | #4: byte budget держится |
| L4 log storm | max-эмиссия logs/events | до предела очередей | #12: drop-newest + счётчик, шаги ≤ T_step |
| L5 I2C slow/stuck | задержка ответа / stuck SCL/SDA | stuck до recovery | #14: recovery ≤ 16 SCL, cooldown ≥ 5 s, Degraded |
| L6 flash erase/program | sector 7 erase + 512 B program | одиночное окно ≤ 4 s | #3/#5: W_flash, ISR-latency в окне |
| L7 max operation steps | генератор bounded steps на полную очередь | максимальное число шагов в тик | #8: суммарное время ≤ T_step, jitter ≤ 1 шаг |
| L8 interrupt load | периодические ISR (TIM) | до 50% ISR-нагрузки | #4/#10: ISR allowance, CPU margin |
| L9 BMS параллель | BMS-транзакции на shared I2C-шине (bus-busy) | DE 12 ms + guard 5 ms | #14: I2C-координация при BMS |

## 5. Fault cases (обязательные, issue 10)

| Fault ID | Инъекция | Ожидаемая реакция | Obligation |
| --- | --- | --- | --- |
| F1 bumper edge | GPIO falling edge (в т.ч. в окне erase) | force-stop min-ID кадр ≤ T_fs; latch + crash counter; в окне - deferral ≤ 4 s (Q7.2) | #3/#13 |
| F2 sensor stale | возраст ToF-образца > T_fresh | stop IMMEDIATE по воронке; T_eso ≤ 70 ms | #1/#2 |
| F3 manual link loss | прекращение hold-to-run | lease expiry → CONTROLLED stop; T_lease_stop = T_step + T_ramp | #6 |
| F4 STOP command | внешний STOP | CONTROLLED/IMMEDIATE по контексту | #1 |
| F5 critical stall | намеренный unbounded callback (тест-хук) | watchdog reload остановлен; reset в аппаратном окне 6.8–18.8 s; boot-причина watchdog | #5 |
| F6 queue overload | push сверх бюджета | reject/drop + счётчик + событие; резерв Control не тронут | #7 |
| F7 power-cut | снятие питания: save / mid-op | валидный предыдущий снапшот; bootcount++; HZ-14-путь | #11 (HIL) |

## 6. Карта obligations → сценарии → бюджеты

Протокол: observed maxima с workload metadata (N ≥ 30 прогонов стартовый минимум, #52 §6.3), не WCET; правило: measured max + margin ≤ бюджет, margin по #48 (C1a ≥ 100 ms; watchdog k ≥ 1.5–2; иначе ≥ 20% или явный отказ с обоснованием).

| Obl | Метрика | Бюджет (#48) | Сценарий | Уровень |
| --- | --- | --- | --- | --- |
| #1 | T_eso = T_check_jitter + T_arb + T_emit; T_sample_worst | ≤ 70 ms; T_sample_worst < 200 ms (C1a) | F2 + L7/L8: stale → CAN стоп-кадр; полная цепочка ≤ 370 ms (T_fresh + T_eso) | target (bench) |
| #2 | T_check_jitter, T_arb | ≤ 1 шаг = 10 ms | L7/L8: измерение длительности bounded шагов | target |
| #3 | W_flash, T_isr (включая вектор-выборку), force-stop в окне erase | W_flash ≤ 4 s; T_fs ≤ 10 ms; deferral ≤ 4 s | L6 + F1: erase с таймерным ISR (DWT); RAM-exec развилка | target |
| #4 | adapter duration bounds | CAN 64/16; UART 230/57/10 B; I2C 1 чтение/8 ms | L1/L3/L5/L9: per-adapter максимумы | target |
| #5 | watchdog под combined load | W_flash + margin ≤ 6.8 s (fast-конец LSI) | L1+L3+L4+L6+L8: combined load; F5: starvation | target |
| #6 | lease → safe stop | T_step + T_ramp (ramp V1-класс ~1.7 s worst) | F3 | target |
| #7 | queue overflow, счётчики, high-water | 18/8/4; 8/32/32/16 | F6: прогон с переполнением, проверка счётчиков | host + target |
| #8 | bounded steps | каждый шаг ≤ 10 ms | L7/L8 + все loads: max step duration | host + target |
| #9 | NTP-скачок не ломает monotonic | wrap-safe, монотонность | host property-тест (инъецированный tick) | host |
| #10 | link map, per-function stack, ISR allowance, heap policy, high-water, CPU margin | CPU ≤ 70%, RAM ≤ 70%, stack +25%, zero heap | сборка + target: map/stack/watermarks | build + target |
| #11 | power-cut: save / update / mid-operation | валидный снапшот, bootcount++, W_apply ≤ 1 s | F7 | HIL (#52 §6) |
| #12 | non-blocking TX (log-storm) | ни один шаг > T_step | L4/L2 | host + target |
| #13 | CAN dual-class TX, RX overflow, flood | control-plane жив, T_fs не нарушен | L1 + F1 | target (bench CAN-пир) |
| #14 | I2C recovery при параллельной BMS | recovery ≤ 16 SCL, cooldown ≥ 5 s; Degraded своевременно | L5 + L9 | target |
| #15 | radio AUX-hang, mode-settle | ожидание неблокирующее; ни один шаг > T_step | AUX hold: split по bounded под-шагам | target (или HIL radio as-needed, #52 §5.3) |

Переводы obligations в следующий шаг (допускаются acceptance #54):

| Obl | Статус на скелете | Куда переведено |
| --- | --- | --- |
| #1/#2 | host: цепочка детерминированно проверена (C1-тесты, µs-трассы); target-измерение T_eso/T_check_jitter/T_arb | bench bring-up (реальные адаптеры) |
| #3 | W_flash/T_isr/force-stop в окне erase | bench bring-up (flash-адаптер, RAM-exec развилка) |
| #4 | adapter duration bounds | bench bring-up (реальные CAN/UART/I2C адаптеры) |
| #5 | watchdog под combined load: host-модель + starvation-тест; target-измерение | bench bring-up |
| #6 | lease → stop: host-тест (CONTROLLED stop в пределах шага); target-измерение T_lease_stop | bench bring-up |
| #8 | bounded steps: host-тесты; target-измерение | bench bring-up |
| #9 | host property-тесты (wrap, backward jump) - закрыто | - |
| #10 | link map + per-function stack (`.su`) на target-сборке; CPU/RAM/stack high-water | bench bring-up (runtime watermarks) |
| #11 | power-cut: save / update / mid-operation | HIL (#52 §6) |
| #12 | log-storm: host-тест (ни один шаг > T_step); target-проверка неблокирующего TX | bench bring-up |
| #13 | CAN dual-class TX/RX/flood: host-тесты (RX overflow, TX budget); физический слой | bench + HIL (CAN-инъектор) |
| #14 | I2C recovery при BMS | bench bring-up (I2C-адаптер + bus-busy) |
| #15 | radio AUX-hang | HIL radio as-needed (#52 §5.3) |

Пока bench-адаптеры не реализованы, target-нога скелета - пустой kernel loop; obligation-измерения #1-#6/#8/#10/#12-#14 явно переведены в bench-шаг (M1-фикс: честный transfer-лист вместо молчаливого отсутствия пути).

## 7. Evidence-протокол

1. **Preliminary budgets и trace IDs ДО измерений** (evidence #1 issue 10): аналитические значения §6 - baseline, не измеренные; каждому измерению предшествует trace ID события (trigger → stop-кадр → timestamp).
2. **Source/static proof** (evidence #2): bounded callbacks, bounded адаптеры, статические очереди, overflow-политики, single-writer ownership - подтверждается include-lint + код-ревью + host-тестами.
3. **Observed maxima с workload metadata** (evidence #3): safety latency, scheduler gap, adapter duration, queue residence, watchdog behavior; workload-матрица фиксируется; результат НЕ называется WCET.
4. **Link map / stack / ISR / heap / high-water / CPU margin** (evidence #4): `-Wl,-Map`, `-fstack-usage`, runtime watermark, CPU margin (idle-counter vs tick).
5. **Host deterministic tests** (evidence #5): unit/property/fault на native, одинаковые векторы для трёх variants; host↔target эквивалентность - oracle O3 (#52 §3).

Формат records - по #52 §7.1: `{ID (V-<n>), тип, refs (obligations), method, oracle, environment, результат, source/version/confidence, owner, дата}`; измерения - с workload metadata. Финальный отчёт - `docs/research/proving-slice-report.md` (после измерений).

## 8. Host ↔ target распределение

| Слой | Среда | Что измеряется/проверяется |
| --- | --- | --- |
| Host (native, `pio test -e native-*`) | CI/local | deterministic core: bounded steps, arbitration порядок, queue overload, freshness FSM, lease FSM (F3), monotonic wrap/backward-jump (#9), C1-цепочка с µs-трассами, свойственные каждому kernel инварианты; fault-тесты F2/F3/F4/F6. **Host-тайминги виртуальные (детерминированные), НЕ execution-evidence** - реальные тайминги только target (DWT); host-нога отвечает за логику и сравнение kernel-семантики (issue 10 evidence #5) |
| Bench (плата без механики) | плата + ST-Link + CAN-пир | тайминги: T_eso, T_check_jitter, T_arb, T_emit, T_fs, W_flash, ISR-latency в окне, adapter bounds, watchdog под combined load, CPU/RAM/stack (обл. #1–#8, #10, #12–#14) |
| HIL (стенд) | CAN-инъектор, питание-реле, I2C-коммутатор, E22+аттенюатор | power-cut (#11), физический CAN (#13), radio AUX-hang (#15) - по #52 §6, as-needed на release/смену класса |

Bench-доступность: bring-up и тайминговые измерения требуют платы контроллера с ST-Link - организационный вход для владельца (см. #54 Notes). До получения платы host-слой полностью исполним.

## 9. Failure-условия пересмотра (проверяются явно, issue 10)

| Условие | Проверка в slice | При срабатывании |
| --- | --- | --- |
| Cooperative не удерживает safety bound под combined load | T_eso/T_fs/T_sample_worst под L1+L3+L4+L6+L8 | запуск сравнения hybrid (приоритетный fallback) |
| Bounded adapter contract невозможен на Arduino/driver stack | адаптеры с бюджетами собираются и держат duration bounds | пересмотр границы адаптеров/драйверов |
| Static resource margin недостаточен | CPU/RAM/stack/queues под combined load | пересмотр бюджетов или модели |
| Несколько независимых hard-deadline activities | evidence отсутствия взаимной preemption-потребности | рассмотрение static RTOS (только по отдельному positive evidence) |

Каждое условие получает строку в отчёте: проверено/не проверено, результат, решение.

## 10. Утилизация proving-кода

`proving/` - временный evidence-код. После решения: либо удаляется с фиксацией ссылок на отчёт (канонический asset), либо выборочно промоутится (адаптеры-прототипы → production-адаптеры, генераторы нагрузок → L4/L5 harness по #52). Решение - на gate приёма отчёта; `platform/execution_*.cpp` variants: выбранный kernel становится основой execution core, остальные удаляются или сохраняются как референс по решению владельца.

## 11. Assumptions / Unknowns / Confidence

- **Assumption**: stock board def `genericSTM32F405RG` воспроизводит поведение V1-платы (#51 §14) - validation obligation bring-up.
- **Assumption**: RAM-exec развилка (flash-окно vs реактивность) решается измерением W_flash и ISR-latency на PCB (#43 §4, #45 §7.1).
- **Unknown**: точная длительность erase/program на этой PCB; bank mode/ART; ISR entry latency в окне.
- **Unknown**: версия googletest при стендапе native env - фиксируется по факту (#51 §14).
- **Unknown**: RTOS-пакет и конфигурация (research RtosVariant, §3.3).
- **Confidence**: host-механизмы - высокая (#51 §14); target-тайминги - аналитические до измерения, не measured.

## 12. Ссылки

- Issue 10 (решение, evidence-список, failure-условия), тикет #54 (этот).
- `docs/software-architecture-boundaries-v3.md` (§8 - 15 obligations, §4 - single-writer, §6 - queue classes).
- `docs/quality-attributes-and-budgets-v3.md` (§2 C1-C6, §3 watchdog, §4 T_step, §6 очереди, §7 адаптерные бюджеты, §8 ресурсы, §11 measurement plan).
- `docs/safety-model-v3.md` (§2 health, §3 инварианты, §4 stop-профили, §6 C1-C6, §7.1 bounds).
- `docs/verification-strategy-v3.md` (§3 oracle O4, §5.1 закрытие obligations, §6 HIL, §6.3 observed-maxima протокол).
- `docs/engineering-and-release-baseline-v3.md` (§3 пины, §4 board, §5 структура, §12 поддержка slice).
- `docs/research/v1-execution-evidence.md`, `docs/research/v1-system-evidence-index.md`.

## 13. Исследовательские факты (research-субагенты, 2026-08-12)

### 13.1 RTOS-кандидат (RtosVariant, source-verified)

- **Факт**: ststm32@17.4.0 предлагает фреймворки mbed/cmsis/spl/libopencm3/arduino/stm32cube/zephyr; выделенного FreeRTOS-фреймворка нет (`platform.json`, проверено по исходникам пакета).
- **Факт**: framework-arduinoststm32@4.20701.0 НЕ содержит FreeRTOS-библиотеки; `cores/arduino/main.cpp:28` ставит `NVIC_PRIORITYGROUP_4` «Required by FreeRTOS»; `libraries/SrcWrapper/src/stm32/clock.c` - strong `SysTick_Handler` с weak-хуком `osSystickHandler()` (точка интеграции тика RTOS); SysTick = HAL tick 1 kHz (HAL_GetTick/millis).
- **Факт** (реестр PlatformIO, API): официального `FreeRTOS/FreeRTOS-Kernel` в реестре нет. In-registry: `mincrmatt12/STM32Cube Middleware-FreeRTOS@10.3.1+f4-1.26.1` (ST-patched, для F4 HAL), `linlin-study/FreeRTOS-Kernel@10.4.4-1` (plain kernel source, 2021), `bojit/PlatformIO-FreeRTOS@2.1.4` (2025, mirror), `feilipu/FreeRTOS@11.1.0-3` (AVR-oriented - НЕ подходит).
- **Решение**: пакет выбирает владелец (HITL, briefing). Рекомендация: ST Cube middleware (соответствие ST-экосистеме F4) при приёме риска «версия 2021 года»; альтернатива - plain kernel 10.4.4.

### 13.2 Инструментация F405 (MeasurementInstr, source-verified)

- **Факт**: CPU 168 МГц (HSI→PLL M=8 N=168 P=2, FLASH_LATENCY_5) для genericSTM32F405RG (`variants/STM32F4xx/F405RGT_F415RGT/generic_clock.c`).
- **Факт**: линкер-скрипт F405RGT_F415RGT уже содержит `*(.RamFunc)` в `.data` - RAM-exec возможен без правки скрипта (только `__attribute__((section(".RamFunc")))`).
- **Факт**: F405 НЕ входит в guarded-список `__RAM_FUNC` HAL (только F410/F411/F446/F412) - RAM-exec только ручным атрибутом секции, не HAL-макросом.
- **Факт**: core предоставляет `stm32/dwt.h` (DWT-хелперы), `stm32/timer.h`, `IWatchdog` (подтверждено: `begin(10000000)` → prescaler 128, reload 2499), `HAL_FLASHEx_Erase` (FLASH_SECTOR_7), `HAL_CAN_AddTxMessage`/`GetRxMessage`, флаги TSR/TME/ROR.
- **Факт**: TIM2_IRQHandler прокинут через SrcWrapper `HAL_TIM_IRQHandler` - интеграция TIM2-тика штатная.
- **Ограничение**: ST.com (RM0090/DS8626 PDF) недоступен с этой машины (timeout); численные bounds уже primary-sourced в `docs/safety-model-v3.md` §7.1 (RM0090 §3.6 stall, DS8626 Table 40 tERASE128KB = 4 s) - evidence сохранён.
- Методы измерения: monotonic - TIM2 32-bit (1 ms), DWT CYCCNT (`CoreDebug->DEMCR TRCENA`, `DWT->CTRL CYCCNTENA`), ISR-latency в flash-окне - периодический таймерный ISR с GPIO-пробником (анализатор) или DWT-дельты onboard, W_flash - HAL erase/program по monotonic, stack - `-fstack-usage` + fill-pattern idle-scan, CPU - idle-счётчик vs tick, IWDG - эмпирический starvation-тест с reset-cause.

### 13.3 Native env (NativeEnv, эмпирически проверено)

- **Факт**: googletest резолвится `^1.17.0` → 1.17.0; реестр содержит только 1.17.0 / 1.15.2 / 1.12.1; точный пин - `lib_deps = google/googletest@1.17.0` (закрывает Unknown #51 §14).
- **Факт**: `platformio==6.1.19` работает на Python 3.14.3 (проверено установкой и прогоном); CI остаётся на ubuntu-24.04 python 3.11.
- **Факт**: `src_dir`/`test_dir` - опции секции `[platformio]`; `build_src_filter`/`build_flags`/`test_build_src` - env-опции. Suite-директории тестов должны начинаться с `test_` (иначе discovery = wildcard и пустая сборка). Каждый suite нуждается в собственном `test_main.cpp` (PlatformIO не поставляет gtest main).
- **Факт**: `pio test` 6.1.19: `--json-output`, `--json-output-path`, `--junit-output-path`; `pio pkg list`/`outdated` - только текст (без JSON) - check_toolchain.py парсит текст.
- **Факт**: `.su`-файлы (`-fstack-usage`) ложатся рядом с `.o` в `.pio/build/<env>/`; относительный `-Wl,-Map=firmware.map` - в корень проекта (абсолютные пути с `${...}` ломались на Windows backslash).
- **Факт**: `-fno-exceptions -fno-rtti -fno-threadsafe-statics` на host GCC + gtest 1.15.2/1.17.0 компилируются (эмпирически).
- **Решение исполнения**: host-леги трёх variants - отдельные env `native-coop` / `native-hybrid` / `native-rtos` одного дерева (интерпретация #51 §12 «один native env, kernel конфигурацией сборки»: CI-нога - `native-coop`; variant-специфичные env - только для сравнительного отчёта #54). Отклонение от буквы #51 фиксируется и выносится владельцу.
