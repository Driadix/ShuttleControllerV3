# Качество и бюджеты V3 (Quality Attributes & Budgets, item 9)

Статус: **утверждено владельцем (гриллинг Q1–Q9, тикет [#48](https://github.com/Driadix/ShuttleControllerV3/issues/48))**. Вход в logical item `Quality Attributes & Budgets` нормативного пакета (issue 8, gate G4) и в architecture proving slice (тикет #54). Каждое число имеет источник и validation obligation; аналитические оценки не называются measured.

Термины — канонические из `CONTEXT.md`. Исполнение — cooperative scheduler с bounded run-to-completion steps (issue 10), ports-and-adapters (#43). Численные safety-дедлайны — каркас C1–C6 из `docs/safety-model-v3.md` (#45).

## 1. Принципы бюджетирования

- **Классы источников** (правило acceptance #48): `hazard-derived` (вывод из safety-модели #45), `code-derived` (конфигурированные/выведенные значения V1 по evidence-активам), `datasheet-derived` (первичные документы ST), `analytical` (композиция предыдущих), `measured` (proving slice #54 + тесты на реальном шаттле).
- **Правило evidence**: analytical-оценка не называется measured; каждое число получает validation obligation (раздел 11), закрывающуюся измерением или явным переводом в следующий шаг.
- **Freshness-суб-бюджеты пред-аллоцированы** решением #43 (ToF 300 ms-класс, AS5600 1 s-класс) и **не перепроектируются** этим документом; учёт цепочки целиком ведёт Safety Authority.
- **Порт-предпочтение владельца** (#45): минимальное отклонение от V1 там, где это не противоречит safety-требованиям.
- **Иерархия**: safety-цепочка → каденции → очереди/адаптеры → ресурсы MCU → startup/availability.

## 2. Safety-цепочка event→safe-output (C1–C6)

Инстанцирование каркаса #45. Формула бюджета соответствует требованию IEC 61508-2 §7.4.4.1.4 (HFT=0, high-demand/continuous): сумма интервала диагностики и времени до safe state < process safety time. Здесь FRT1 := `T_fresh + T_check_jitter` (детекция), FRT2 := `T_arb + T_emit` (приведение к safe state на выходе), D_brake — остановочная дистанция привода (вне firmware, параметр commissioning).

| Параметр | Значение | Класс источника | Validation obligation |
| --- | --- | --- | --- |
| `T_fresh` (ToF, направленная) | 300 ms — решено #45 (O3) | hazard-derived | — |
| `T_sample_worst` (ToF) | RR 4 сенсора × 8 ms = 32 ms + BMS-quiet 17 ms + radio-паузы ⇒ **≤ 100 ms** (analytical) | code-derived + analytical | #54 #1/#2; C1a: `T_sample_worst + margin < 300 ms` |
| `T_step` (bounded step) | **10 ms** (Q5) | analytical | #54 #4/#8 |
| `T_check_jitter` | ≤ 1 bounded step = 10 ms | analytical | #54 #2 |
| `T_arb` | ≤ 1 bounded step = 10 ms | analytical | #54 #2 |
| `T_emit` (до ближайшего TX-слота) | ≤ control TX каденция = 50 ms | code-derived (V1 gate 50 ms) | #54 #1 |
| `T_eso` = jitter+arb+emit | ≤ **70 ms** | analytical (не measured) | #54 #1, #5 |
| `T_fs` (force-stop) | `T_isr + T_step + T_mailbox`; budget ≤ 10 ms | analytical | #54 #3/#13 |
| `T_lease_stop` | `T_step + T_ramp`; ramp V1-класс ≈ 1.7 s worst при maxSpeed (48 шагов × ~35 ms) | code-derived + analytical | #54 #6 |
| `W_flash` | ≤ 4 s (datasheet x8, сектор 128 KB) | datasheet-derived | #54 #3 (измерение на PCB) |
| `D_brake(v, load, grade)` | кривая, параметр commissioning (INV-BRAKE-VALIDITY) | measured (обязательство) | #54 #1 + полевые тесты |
| `T_deg` | **60 s** → Fault/stop (Q2) | hazard-derived (решение) | host: health FSM |
| `T_bms_stale` | **60 s** (Q3) | code-derived + решение | host: staleness-логика |
| ATEMP пороги | **warn 90 °C / fault 110 °C**, гистерезис 5 °C (Q4) | решение (HZ-16) | host + полевые данные |
| Stall no-progress | `W_np` = 5000 ms, `T_np` = 10 mm за окно (Q9) | code-derived + решение | host: no-progress логика; #54 #8 |

### 2.1 C1 и вилка C1b — РЕШЕНО (Q1)

`v_max × (T_fresh + T_check_jitter + T_arb + T_emit) + D_brake(v_max) ≤ D_accept`, где:

- `T_fresh + T_eso = 370 ms` (analytical);
- **`D_accept = 1.0 м`** (решение владельца, Q1);
- при типовой оценке v_max 1.0–1.7 м/с (assumption #45) слепая зона цепочки: 0.37–0.63 м; остаток на D_brake при 1.7 м/с: **≤ 0.37 м**.

**Правило вилки C1b**: интервал `T_sample_worst + margin < 300 ms` (левая часть) подтверждается измерением #54 #1/#2. Правая часть проверяется на измеренных `v_max_phys` (mapping percent→м/с живёт в приводе; measurement obligation) и `D_brake`: если при текущем maxSpeed интервал пуст (`D_brake > 0.37 м` при 1.7 м/с) — **снижение maxSpeed** (порт-предпочтение) до выполнения неравенства; расширение D_accept допускается только с геометрическим обоснованием (зазор до препятствий) и impact review по #45 (O3-приёмка условна на эту вилку). Снижение `T_fresh` ниже 300 ms — не рассматривается (O3, V1 keep).

### 2.2 Degraded (T_deg) — РЕШЕНО (Q2)

Потолок скорости ≤ 1.0 м/с (F5, #45) остаётся. Добавлен временной bound: **непрерывное пребывание в Degraded > 60 s ⇒ переход Fault/stop** (IEC 61508-2 §7.4.8.3: HFT=0, high-demand — детекция опасного отказа требует specified action; неограниченная degraded-работа на одноканальном safety-пути не имеет стандартного основания). Degraded-условие снимается раньше — Fault не наступает. Новый fault-код (`FAULT_DEGRADED_TIMEOUT`) — в wire-реестр #47. Recovery — по правилам #45 (снятие условия + квалификация). Таймер считает monotonic-время (NTP-скачок не влияет, #43).

## 3. Watchdog

- **Формула** (#43): окно = `max(худший bounded шаг, flash-окно, ISR-время) + margin`.
- **Окно = 10 s** (Q5, V1 keep): `max(10 ms, 4 s, ISR) + 6 s margin` — margin 150% от datasheet-худшего W_flash (практика k ≥ 1.5–2; IEC 60730-1 Class B: открытое окно ≥ 50% периода — удовлетворено).
- **Роль**: backstop (INV-WATCHDOG-ARMED), не первичный safety-путь — первичный путь арбитражная цепочка C1 (ms-класс). IWDG — простой watchdog с независимым тактированием без временного окна ⇒ Low DC (IEC 61508-2 Table A.10) — приемлемо для backstop-роли; перенос детекции на watchdog не бюджетируется.
- **Аппаратный факт** (datasheet-derived, #45 §7.1): LSI 17/32/47 kHz ⇒ аппаратный timeout 22.3–61.7 s (worst при 17 kHz); reload-каденция — нормативное требование, аппаратный сброс — верхняя страховка.
- **Reload**: execution core на каждой границе bounded шага и в idle-loop; в TX-wait циклах (V1-практика); **в update path обязателен reload между sector-erase** (2×4 s < 10 s, 3×4 s = 12 s > 10 s — три последовательных эрейза без reload сбрасывают IWDG).
- **Validation**: измеренные `W_flash + T_step + T_isr + margin ≤ 10 s` (#54 #5). Превышение ⇒ пересмотр окна (G4, impact #45).

## 4. Bounded step budget

- `T_step = 10 ms` (Q5): каждый domain-шаг и adapter-шаг ≤ 10 ms. Исключение — flash-окно (quiescent, отдельный контракт, ≤ 4 s).
- Следствия: `T_check_jitter ≤ 1 шаг` (INV-SENSING-FRESH на границе шага), `T_arb ≤ 1 шаг`; 30 шагов укладываются в T_fresh.
- ToF-слот 8 ms < T_step (чтение ~1.2–1.5 ms на 100 kHz — code-derived, укладывается с запасом); AS5600 — 250 ms каденция, не в каждом шаге.
- Validation: #54 #4 (adapter duration bounds), #8 (bounded steps под combined load).

## 5. Каденции (V1 keep, code-derived)

| Сервис | Каденция | Примечание |
| --- | --- | --- |
| Control TX (CAN 100/101) | 50 ms gate; ramp шаг 2 ед / ~35 ms | V1 keep (Q5) |
| ToF round-robin | слот 8 ms, 4 сенсора ⇒ 32 ms per sensor; I2C 100 kHz | BMS-quiet 17 ms (12 ms DE + 5 ms guard) |
| AS5600 | 250 ms service; stale 1 s (пред-аллоцировано #43) | — |
| BMS | 1 / 5 / 15 / 60 / 5 s (startup/idle/active/high-load/low-batt); DE 12 ms; RX timeout 140 ms | T_bms_stale = 60 s (Q3) |
| Radio | backoff 5/30/120/600 s; silence probe 15 s; audit 300 s; AUX 20 ms; RSSI fresh 10 s | stationary+idle |
| Telemetry (default подписки) | 300 ms; sensors 500 ms; stats 5 s; link health 5 s | V3: subscription-driven; defaults при подписке |
| Startup grace | 1 s; Ready ≤ 5 s (Q7) | INV-STARTUP-GATE |

## 6. Очереди и overload (числа — Q5)

MTU = **128 B** (V1 keep, оба профиля). Бюджеты фиксированные (глубина, кадров/байт за тик); политики overload — #43/#47; enforcement — механическое в adapters/sink.

| Очередь | Глубина | Overload (#43/#47) |
| --- | --- | --- |
| Control (in) | 16 фреймов | reject на admission (явный код) |
| Service (in) | 8 | reject на admission |
| Update (in) | 4 (резерв 2 при in-progress) | pause; reject только новые транзакции |
| telemetry (out) | 8 | drop-oldest (свежесть) |
| events (out) | 32 | резерв ёмкости; drop-newest |
| logs (out) | 32 | резерв ёмкости; drop-newest |
| traces (out) | 16 | drop-oldest; reserved-флаг для fault-correlated |

- Каждый drop/reject ⇒ счётчик (Observability Producer) + событие (#43).
- **authorityId budget = 16** concurrent principals на effective `network_bridge` (Q5); исчерпание ⇒ `BusyRejected` (#47 §5.1 п.5).
- RAM-стоимость очередей: ~14 KB суммарно (worst 16×128 B + 3×32×128 B + …) — в пределах бюджета RAM (раздел 8).
- Sizing-основание: burst = handshake + N principals × pending (Control 16 ≥ 16 authority-держателей × 1); events/logs 32 = ~1.5–2× типовой поток диагностики за 10 s (практика); telemetry 8 = 2.7 s при 300 ms каденции.
- Validation: #54 #7 (переполнения под load, счётчики наблюдаемы), #10 (high-water).

## 7. Адаптерные бюджеты (per-tick, Q5)

| Адаптер | Бюджет за тик (10 ms) | Основание |
| --- | --- | --- |
| CAN RX drain | ≤ 64 кадров/тик | 500 kbit/s ⇒ ≤ ~38 кадров/тик worst (8-байт); 64 покрывает backlog |
| CAN TX | ≤ 16 кадров/тик | bounded producer (Actuator 100/101 + status) |
| UART bridge (230400) | ≤ 230 B/тик RX+TX | baud-derived; ~1.8 фрейма/тик |
| UART radio (57600) | ≤ 57 B/тик | baud-derived |
| UART BMS (9600) | ≤ 10 B/тик | baud-derived; транзакция по контракту BMS |
| I2C | 1 ToF-чтение / 8 ms слот; AS5600 / 250 ms; BMS-quiet guard | V1 keep; recovery ≤ 16 SCL + cooldown ≥ 5 s |
| Flash | W_flash ≤ 4 s (datasheet); save = 128 word-program + 1 B инвалидация + `delay(1000)` V1-класс | quiescence C6; RAM-exec развилка по измерению #54 |
| Update (network_bridge) | ≥ 100 KB/s staging (230400 ⇒ ~1.8 MTU/тик); reload watchdog между sector-erase | Q5; W_apply измеряется (#50/#54) |

- Блокирующий TX запрещён (DMA-TX или producer-budget, #43); доказательство неблокирующего TX — #54 #12.
- Бюджеты адаптеров — вход proving slice #54 (обязательства #4, #13–#15).

## 8. Ресурсы MCU (Q6)

- **CPU ≤ 70%** worst-case (measured; Liu & Layland RM-граница 69.3%; практика NASA GSFC late-stage); целевой типичный уровень ≤ 50%. Jitter ≤ 1 bounded step.
- **RAM ≤ 70%** статической памяти (margin ≥ 30%); **zero heap после init** (V1: Arduino `String` в калибровке — запрещено в critical paths; R04).
- **Stack**: watermark (Keil AN316) + **25% headroom** + worst-case ISR nesting (ZVEI 20–25%; ≥ 200 B запас NuttX).
- **Flash**: journal sector 7, 128 KB (V1 keep); application image budget **≤ 512 KB** (ограничение для slot/staging-дизайна #50); W_apply — измерение.
- Validation: #54 #10 (link map, per-function stack, ISR allowance, heap policy, high-water, CPU margin).

## 9. Startup и availability

- **Startup**: Ready ≤ **5 s** после power-up (Q7): grace 1 s + requalification + journal scan + сенсорные probes; движение запрещено до Ready (INV-STARTUP-GATE). Reset-cause-aware вход (persisted marker, #45 Q5 A).
- **Availability**: ≥ **99.5%** годовой доступности control plane (Q8) — uptime-счётчики, reset categories, watchdog resets (V1 stats keep); измерение — Observability (item 8, тикет #49).
- **Determinism**: monotonic tick 1 ms; все доменные таймауты от monotonic (#43); NTP-скачок не влияет (validation #54 #9).

## 10. Решения владельца (Q1–Q9)

| ID | Решение |
| --- | --- |
| Q1 | D_accept = 1.0 м; правило вилки C1b: снижение maxSpeed при пустом интервале, расширение — только с геометрическим обоснованием |
| Q2 | T_deg = 60 s непрерывного Degraded ⇒ Fault/stop; новый fault-код в #47 |
| Q3 | T_bms_stale = 60 s (вместо V1 300 s); HZ-17 warning-семантика без изменений |
| Q4 | ATEMP warn 90 °C / fault 110 °C, гистерезис 5 °C; residual HZ-16: запас 15 °C до max junction 125 °C — принят владельцем |
| Q5 | Технический пакет: watchdog 10 s; T_step 10 ms; control TX 50 ms; MTU 128 B; очереди 16/8/4 и 8/32/32/16; authority 16; CAN RX/TX 64/16; UART 230/57/10 B/тик; update ≥ 100 KB/s |
| Q6 | CPU ≤ 70% / RAM ≤ 70% / stack +25% / zero heap post-init |
| Q7 | Startup: Ready ≤ 5 s |
| Q8 | Availability ≥ 99.5% |
| Q9 | Stall: W_np = 5000 ms, T_np = 10 mm (V1-класс); скорость-масштабирование — только если proving покажет ложные срабатывания |

## 11. Measurement obligations (план для #54 и полевых тестов)

Каждый analytical-бюджет закрывается измерением; каждое число получает источник и validation obligation (acceptance #48). Метод — issue 10 (proving slice, три kernel variants, synthetic loads) + тесты на реальном шаттле в процессе переноса (решение владельца #45).

| Obligation | Параметр | Метод / место |
| --- | --- | --- |
| #54 #1 | `T_eso`, `T_sample_worst`, `D_brake`, слепая зона C1 | proving slice: event→safe-output; полевые: торможение |
| #54 #2 | `T_check_jitter`, `T_arb`, freshness-суб-бюджеты | proving slice: bounded steps |
| #54 #3 | `W_flash`, `T_isr` (включая вектор-выборку), force-stop в окне erase | proving slice: flash + ISR latency; RAM-exec развилка |
| #54 #4 | adapter duration bounds (CAN/I2C/UART/flash) | proving slice |
| #54 #5 | watchdog под combined load; `W_flash + margin ≤ 10 s` | proving slice |
| #54 #6 | lease timeout → safe stop latency | proving slice |
| #54 #7 | переполнения очередей, счётчики, high-water | proving slice |
| #54 #8 | bounded steps; stall-детекция | proving slice |
| #54 #9 | NTP-скачок не ломает monotonic | host property test |
| #54 #10 | link map, per-function stack, CPU margin, RAM high-water | build + target |
| #54 #11 | power-cut: save / update / mid-operation | HIL (см. #52) |
| #54 #12 | неблокирующий TX (log-storm) | proving slice |
| #54 #13 | CAN dual-class TX, RX overflow, flood | proving slice |
| #54 #14 | I2C recovery при параллельной BMS | proving slice |
| #54 #15 | radio AUX-hang, mode-settle | proving slice |
| field | `v_max_phys` (percent→м/с mapping привода) | тесты на реальном шаттле |
| field | ATEMP-пороги при типовой нагрузке | телеметрия, полевые данные |
| field | availability 99.5%, uptime/reset статистика | Observability, полевой сбор |

## 12. Условия пересмотра

- Измеренный `W_flash > 4 s` или `W_flash + margin > 10 s` ⇒ пересмотр watchdog-окна (G4, impact #45).
- Измеренный `D_brake > 0.37 м` при v_max ⇒ снижение maxSpeed по правилу C1b; расширение D_accept — только с геометрическим обоснованием (impact #45, O3-приёмка).
- Proving slice не удерживает safety-бюджеты под combined load ⇒ пересмотр execution architecture (условия issue 10).
- Новый профиль шаттла (после 800/1000/1200) ⇒ пересчёт геометрии, D_brake, stall-порогов, D_accept.
- T_deg/Fault-код: добавление `FAULT_DEGRADED_TIMEOUT` в wire-реестр — #47 (item 6, gate G3).

## 13. Ссылки

- Тикет «Определить NFR и resource budgets V3» (#48) и resolution-комментарии.
- `docs/safety-model-v3.md` (#45) — каркас C1–C6, O3, Degraded, HZ-16/HZ-17, measurement-resolved параметры.
- `docs/software-architecture-boundaries-v3.md` (#43) — freshness-суб-бюджеты, watchdog-формула, классы очередей, 15 validation obligations.
- `docs/external-semantic-transport-contracts-v3.md` (#47) — MTU/бюджеты deferred → этот документ; authorityId, BusyRejected.
- Issue 8 — item 9, gate G4; issue 10 — proving slice; тикет #54 — исполнение измерений.
- V1 evidence: системный индекс (`a7f927c`), execution evidence (`22b8990`), research-фактшит семантики скоростей/конфигурации (commit `4a226e5`).
- Индустриальные источники: IEC 61508-2:2010 (§7.4.4.1.4, §7.4.8.3, Table A.10), IEC 60730-1 Annex H (Class B), ISO 13849-1:2023, ISO 3691-4:2023 (Annex A), IEC 61800-5-2:2016, Liu & Layland 1973, NASA SWEHB 9.12, ZVEI Best-Practice v2, Keil AN316, AUTOSAR SWS DEM.
