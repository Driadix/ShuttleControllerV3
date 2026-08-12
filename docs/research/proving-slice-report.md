# Proving slice V3: сравнительный отчёт (host-only)

Статус: **финальный отчёт тикета [#54](https://github.com/Driadix/ShuttleControllerV3/issues/54)**, host-only scope (решение владельца 2026-08-12). Ветка: `proving/slice-scaffold`, коммиты `03f0a2f`…`a6487d6`.

**Ключевое ограничение (честность evidence)**: целевая плата и HIL-стенд недоступны в этой карте. Все результаты ниже - host-детерминированные тесты, host-simulation с физически обоснованными параметрами и статический анализ; **ни один результат НЕ является target-измерением** и не называется measured (правило #48 §1). Target-валидация бюджетов - implementation-карта (переводы в §4).

## 1. Что сравнивалось

Три execution-модели issue 10 на одном harness (одинаковые шаги, нагрузки, fault-кейсы; отличается только исполнение):

| Variant | Исполнение | Host-статус |
| --- | --- | --- |
| Cooperative | bounded run-to-completion шаги, один loop, reload на границах шагов + idle; ISR только пишут в кольца | ✅ детерминированные тесты (40) |
| Hybrid | cooperative-ядро + прерываемый force-stop путь: min-ID кадр эмитится **из ISR** (T_fs = T_isr + T_mailbox, C4) | ✅ детерминированные тесты (40, +2 hybrid) |
| Static RTOS (FreeRTOS, ST Cube middleware, решение владельца) | 4 задачи с приоритетами, статическая аллокация, force-stop через ISR→task-notification (деферрал) | ⚠️ сборка target + статический анализ; host-порта нет - исполнение не проверяемо без железа (ограничение сравнения) |

Сравнение RTOS на host **невозможно честно** (официальный host-порт отсутствует в реестре, политика #51 §5.4 - только registry-пины). Host-лега RTOS покрывает kernel-независимую логику (арбитраж, health, очереди - те же 40 тестов, ядро выбирается конфигурацией).

## 2. Результаты host-тестов (детерминированные)

80/80 PASSED (native-coop + native-hybrid). Покрытие:

- **Arbitration funnel** (5): тотальный порядок SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT; stop никогда не отклоняется; safety-motion не вытесняет safety-stop; force-stop выигрывает.
- **SafetyHealth** (5): Initializing→Ready→Degraded→Fault; T_fresh 300 ms; T_deg 60 s; qualified recovery; latch до явного reset.
- **Очереди** (5): Control 16+2 резерв; reject на admission; drop-oldest (telemetry) / drop-newest (events/logs); счётчики наблюдаемы (obligation #7).
- **Cooperative kernel** (7): bounded steps, дедлайны, overload наблюдаем, watchdog reload на границах + idle, idle-счётчик (CPU margin).
- **Watchdog starvation** (2): модель 6.8 s fast-конца LSI; пропуск reload → starved; восстановление.
- **Monotonic** (3): wrap 2^64, backward-jump clamp, gap корректен (obligation #9).
- **C1-цепочка** (3): stale→IMMEDIATE stop intent в funnel; stop-кадр эмитится; health деградирует; цепочка в одном тике (µs-трасса).
- **Lease (F3)** (2): expiry → CONTROLLED stop (obligation #6); link_loss.
- **Combined load** (1): 64 шага + log-storm 40 + RX 80 + TX 20 на тик: ни один шаг > T_step; переполнения наблюдаемы (#7/#8/#12/#13-host).
- **Scenario runner** (2): recorder заполняется (metric #1/#2, workload metadata); stale→stop в бюджете.
- **Hybrid force-stop** (2): min-ID кадр эмитится синхронно на фронте (ISR-семантика); каждый фронт эмитит (Q7.2 collapse - только coop-деферрал).
- **Sim-budgets** (4): см. §3.

## 3. Host-simulation с физически обоснованными параметрами

Модель: wire-time vs CPU-cost (дизайн-док §14). Параметры: CAN 108 бит/кадр @ 500 kbit/s = 216 µs; I2C ToF read ~1.26 ms (блокирующий Wire, V1-класс); flash erase 4 s / program 12.8 ms (DS8626 Table 40); UART baud-производные; драйверные CPU-cost (drain 1-2 µs/кадр-байт).

| Сценарий | Симулированный результат | Бюджет (#48) | Вердикт |
| --- | --- | --- | --- |
| C1-цепочка (stale→stop) | T_eso ≈ 1.5 ms (I2C read + CAN TX) | T_eso ≤ 70 ms; T_fresh+eso ≤ 370 ms | ✅ с запасом > 45× |
| Flash-окно | W_flash = 4 s (quiescent) | окно ≤ 6.8 s watchdog fast-end | ✅ запас 2.8 s |
| CAN flood | RX drain 80 кадров × 2 µs = 160 µs | T_step = 10 ms | ✅ с запасом > 60× |
| Combined load (тик) | sensing 1.26 + CAN RX 0.128 + UART 0.23 + TX 0.016 ≈ 1.64 ms | T_step = 10 ms | ✅ с запасом > 6× |

Значения - host-simulation (модельные), не measured; используются как проверка непротиворечивости аналитических бюджетов при worst-case параметрах периферии.

## 4. Облигации #43: закрыто / переведено

| Obl | Статус | Детали |
| --- | --- | --- |
| #7 queue overload | ✅ закрыто (host) | счётчики, drop-политики, high-water наблюдаемы |
| #8 bounded steps | ✅ закрыто (host, conditional) | шаги ≤ T_step под combined load; target-подтверждение - implementation-карта |
| #9 NTP/monotonic | ✅ закрыто (host) | wrap, backward-jump, gap |
| #12 non-blocking TX | ✅ закрыто (host, conditional) | log-storm: ни один шаг > T_step; target - implementation-карта |
| #10 static evidence | ✅ частично | link map, per-function stack (.su), RAM 1.6/1.7/7.1%; runtime high-water - implementation-карта |
| #1 T_eso / C1 | ⏸ host-sim + conditional | host-трассы в бюджете; target-измерение - implementation-карта (L4) |
| #2 freshness суб-бюджеты | ⏸ host | цепочка в одном тике; target - L4 |
| #3 flash + ISR latency | ⏸ host-sim | W_flash модель; RAM-exec развилка документирована; target - L4 |
| #4 adapter duration bounds | ⏸ host-sim | CPU-cost модели; target - L4 |
| #5 watchdog combined | ⏸ host | модель 6.8-18.8 s + starvation; target - L4/L5 |
| #6 lease→stop | ⏸ host | CONTROLLED stop тест; T_lease_stop target - L4 |
| #11 power-cut | ⛔ переведено | L5 (HIL) - implementation-карта |
| #13 CAN физический слой | ⏸ host-часть | dual-class TX/RX/flood host; физика - L4/L5 |
| #14 I2C recovery | ⏸ host-часть | recovery FSM-модель (≤16 SCL, cooldown ≥5 s); stuck-инъекция - L5 |
| #15 radio AUX-hang | ⛔ переведено | L5 as-needed - implementation-карта |

## 5. Failure-условия issue 10 (явная проверка)

| Условие | Проверка на host-only slice | Статус |
| --- | --- | --- |
| Coop не удерживает safety bound под combined load | host-sim C1-цепочка + combined load укладываются с запасом | **не опровергнуто** (host); target-подтверждение - implementation-карта |
| Bounded adapter contract невозможен на Arduino/driver stack | статический анализ core (IWatchdog/HardwareTimer/HAL bxCAN): контракты выразимы; F405 не в __RAM_FUNC HAL - RAM-exec ручным атрибутом | **не опровергнуто** (source-verified); интеграция - implementation-карта |
| Static resource margin недостаточен | линкер: RAM 1.6/1.7/7.1%, flash OK, stack .su | **не опровергнуто** (host/static) |
| Несколько независимых hard-deadline activities | аналитически: единственная safety-цепочка C1 (свежесть→арбитраж→эмиссия) | **не выявлено** (аналитически) |

## 6. Рекомендация

**Cooperative scheduler остаётся нормативной execution architecture V3 (условно, как в issue 10).** Host-evidence не опровергает условный выбор: bounded steps держат бюджеты под combined load (sim + детерминированные тесты), host-simulation C1-цепочки укладывается в T_eso/T_fresh с большим запасом, static resource margins достаточны. Сильнейший аргумент против: target-тайминги не измерены - условность решения сохраняется до L4/L5-валидации на implementation-карте. Hybrid остаётся предпочтительным fallback (ISR-эмиссия force-stop структурно реализована и host-верифицирована); static RTOS - только при отдельном positive evidence (сборка реализована, исполнение не проверено).

## 7. Ограничения отчёта

- Host-тайминги виртуальные/модельные; O4 (measured-vs-budget) к host не применяется.
- RTOS-исполнение не проверяемо без железа (нет host-порта).
- Firmware-сборки (coop/hybrid/rtos) линкуются, но не верифицированы на железе (#51 §15 validation obligation остаётся открытой).
- ControllerV6 PCB-референс зафиксирован (KiCad + BOM) как ресурс bring-up; V6 vs production-плата сверка - Unknown.

## 8. Evidence records (V-<n>)

| ID | Тип | Refs | Метод | Oracle | Среда | Результат |
| --- | --- | --- | --- | --- | --- | --- |
| V-1 | test | #7/#8/#12 | host unit/integration | O1/O2 | native-coop/hybrid | 80/80 PASSED |
| V-2 | analysis | #10 | link map + .su + линкер | - | build | RAM 1.6/1.7/7.1% |
| V-3 | analysis (sim) | #1/#2/#3/#4/#5/#8 | host-simulation | O1 + бюджеты #48 | native | §3 таблица |
| V-4 | review | #54 (весь scaffold) | independent review | - | - | 1 BLOCKING + 5 MAJOR + 5 MINOR + NIT, все устранены |
| V-5 | decision | scope | HITL владельца | - | - | host-only; переводы §4 |

## 9. Ссылки

- Дизайн-док: `docs/proving-slice-v3.md` (host-only scope, §6 переводы, §14 sim, §15 ControllerV6).
- Коммиты: `03f0a2f`, `3dcec90`, `09cb3ab`, `d6fdeca`, `4afdcd6`, `a6487d6`.
- Входы: issue 10, #43 §8, #48, #45, #51, #52.
