# Дизайн sensing-слайса V3 (ToF + AS5600 acquisition, snapshots, freshness, recovery)

Статус: **design-артефакт для тикета [«Реализовать sensing-слайс ToF и AS5600»](https://github.com/Driadix/ShuttleControllerV3/issues/63)**. Вход в реализацию - один vertical PR (Фаза 1: Sensing Service каркас + I2C HAL по `docs/implementation-plan-v3.md` §2 Фаза 1).

Документ наследует утверждённые решения и **не пересматривает** их: #10 (cooperative scheduler с bounded steps), #43 (границы, single-writer, порты, freshness-суб-бюджеты), #45 (safety-модель, INV-SENSING-FRESH), #48 (бюджеты C1-C6: T_fresh 300 ms, AS5600 1 s, I2C-слот 8 ms, recovery ≤ 16 SCL + cooldown ≥ 5 s, T_step 10 ms), #51 (coding profile R1-R8, структура domain/adapters/platform), #70 (kernel: bounded steps, SafetySlot, процесс планирования). Численные бюджеты - из `docs/quality-attributes-and-budgets-v3.md`; термины - канонические из `CONTEXT.md`; контракты чтения датчиков - V1 evidence index (`docs/research/v1-system-evidence-index.md`) и bench record (`docs/l4-sensor-bench-v3.md`).

**Ключевое архитектурное правило (наследует #70):** sensing-работа исполняется строго в foreground как bounded steps через kernel::schedule (≤ 1 шаг/тик, deadline в [now, now+T_step]); из ISR ничего не вызывается (R2). I2C-транзакции - единственный владелец шины в Фазе 1 (BMS-адаптера нет; Busy-семантика порта сохраняется для Фазы 2).

---

## 0. Решения владельца (HITL-брифинг тикета #63)

> Заполняется после брифинга владельца (см. resolution-комментарий тикета). Прототип-факты: `bench/sensing-proto/` (коммит dcb62b3); live-run на стенде 2026-08-13 не завершён - 5V/GNDREF сторона стенда обесточена, владелец уточняет физическое состояние (см. §10).

## 1. Место в архитектуре

```mermaid
flowchart LR
    subgraph platform["platform/ (склейка)"]
        EC["Execution Core<br/>kernel: StepRing + lifecycle"]
    end
    subgraph domain["domain/ (host-deterministic)"]
        SS["Sensing Service<br/>acquisition scheduling, snapshots,<br/>freshness, typed faults"]
        SA["Safety Authority<br/>(#71, Фаза 2)"]
    end
    subgraph adapters["adapters/ (Arduino Core)"]
        I2C["I2C adapter<br/>ToF/AS5600 read, recovery"]
    end
    EC -->|"kernel::schedule (bounded steps)"| SS
    SS -->|"v3::I2cPort::read/recover"| I2C
    SS -->|"snapshot (raw, age, state)"| SA
    SA -->|"SafetySlot::tick (Фаза 2)"| EC
```text

| Элемент | Компонент (#43 §2) | Владение | Примечание |
| --- | --- | --- | --- |
| Sensing Service | domain | acquisition scheduling (ToF RR 8 ms слот, AS5600 250 ms), sensor ownership (4 ToF + AS5600), timestamped snapshots, freshness classification, typed faults/warnings, recovery-координация | единственный владелец расписания I2C-слотов (#43 §4, #48 §7) |
| I2C adapter | adapters (HAL) | транзакции Wire (100 kHz), классификация статусов (ok/noack/short/unknown), recovery-механика (reinit + ≤16 SCL + cooldown ≥5 s, #48 §7, obligation #14) | реализует v3::I2cPort; только foreground |
| Ports | domain | v3::I2cPort (production-форма slice::I2cPort), SensorSnapshot/типы | dependencies внутрь (#43, #51 §5) |

**Граница модуля (#63)**: Sensing Service + I2C adapter + порты + snapshots/freshness/fault-типы + L4-наблюдаемость (RAM read-back сценарий runner). **НЕ входят**: операционные алгоритмы движения (тикет Scope), Safety Authority health-FSM (#71), Observability Producer/UART (#72), BMS-адаптер (свой слайс), CAN-адаптеры.

**ISR-граница (инвариант)**: в scope #63 новых ISR нет. I2C-транзакции блокирующие (Wire, no-DMA) - исполняются только внутри bounded шага (≤ T_step), никогда из ISR (R2). Длительность одного ToF-чтения ~1.2-1.5 ms на 100 kHz (#48 §4 code-derived) - укладывается в слот 8 ms и шаг 10 ms.

## 2. Модели данных

Все типы - fixed-width (`stdint`, R3), без динамической аллокации (R1), bounded (R4).

### 2.1 I2C-порт (production-форма)

```cpp
// domain/ports.h - production-форма (решение владельца §0; эволюция
// slice::I2cPort #43). Один владелец слотов - Sensing Service; Busy
// резервируется для BMS TX window / radio audit (Фаза 2, #48 §7).
namespace v3 {

enum class I2cResult : std::uint8_t {
    Ok = 0,          // транзакция завершена (STOP), данные валидны
    NoAck = 1,       // устройство не ответило (отсутствует/обесточено) - без recovery
    Short = 2,       // read-фаза недопоставила - без recovery
    Busy = 3,        // шина занята другим владельцем (BMS TX / radio audit, Фаза 2)
    Stuck = 4,       // шина в недопустимом состоянии (HAL BUSY/TIMEOUT/ERROR) - recover()
    Recovered = 5,   // после recover(): шина восстановлена
};

struct I2cPort {
    // Одна bounded транзакция: write reg, restart, read len (STOP в конце).
    // Никогда не блокирует дольше слота. Status-классификация V1
    // (TOF_Sense.cpp): noack (адрес/данные), short (недополучено),
    // stuck (BUSY/TIMEOUT/ERROR - кандидат на recover()).
    virtual I2cResult read(std::uint8_t device, std::uint8_t reg,
                           std::uint8_t* out, std::uint8_t len) = 0;
    // Reinit + <=16 SCL pulses + STOP + cooldown >=5 s (#48 §7, obligation
    // #14). Вызывается только Sensing Service, только foreground.
    virtual I2cResult recover() = 0;
    // Сырой статус последней транзакции (диагностика/evidence).
    virtual std::uint8_t last_wire_status() const = 0;
};

}
```text

### 2.2 Sensor identity и snapshot

```cpp
// domain/sensing.h - sensor ownership (адреса - аппаратный контракт #73:
// TOF_BASE_I2C_ADDR 0x08 + id => 0x09..0x0C; AS5600 0x36)
namespace v3::sensing {

enum class SensorId : std::uint8_t {
    TofChannelReverse = 0,   // ID1, 0x09
    TofChannelForward = 1,   // ID2, 0x0A (на стенде не подключен - NACK, #73)
    TofPalletReverse  = 2,   // ID3, 0x0B (на стенде не подключен - NACK, #73)
    TofPalletForward  = 3,   // ID4, 0x0C
    As5600            = 4,   // 0x36
    Count = 5,
};

// Typed faults/warnings (доменные; wire-маппинг - registry #47 §16.4,
// фиксируется при готовности реестра; V1-прецедент: fault/warning биты
// telemetry + WARN_I2C_RECOVERY, evidence index).
enum class SensorFault : std::uint8_t {
    None = 0,
    NoAck = 1,        // устройство не отвечает (транспорт)
    Stale = 2,        // свежесть потеряна (age >= budget)
    BusStuck = 3,     // шина в недопустимом состоянии (требует recover())
};

enum class HealthState : std::uint8_t {   // V1 keep (monitors)
    Starting = 0, Healthy = 1, Degraded = 2, Faulted = 3, Recovering = 4,
};

// Timestamped snapshot: последний успешный sample + возраст + состояние.
// fixed-width, no allocation (R1); читается потребителями (Safety Authority
// #71, Observability #72) в foreground.
struct SensorSnapshot {
    std::uint32_t raw;            // ToF: distance mm (dis @0x24); AS5600: angle (RAW @0x0C)
    std::uint32_t raw2;           // ToF: signal_strength @0x2A; AS5600: ANGLE @0x0E (processed)
    std::uint64_t sample_ms;      // monotonic момент успешного sample (0 = никогда)
    std::uint32_t age_ms;         // now - sample_ms (0xFFFFFFFF если никогда)
    HealthState state;
    SensorFault fault;
    std::uint8_t consecutive_failures;
    std::uint8_t consecutive_successes;
    std::uint8_t last_status;     // I2cResult/статус последней попытки
};

struct SensingConfig {
    // Бюджеты (#48 §2, §5; pre-allocated #43): НЕ перепроектируются.
    std::uint32_t tof_slot_ms = 8;        // 1 ToF чтение за слот, RR по 4
    std::uint32_t as5600_service_ms = 250;
    std::uint32_t tof_fresh_ms = 300;     // T_fresh класс (O3 #45)
    std::uint32_t as5600_fresh_ms = 1000;
    std::uint8_t fault_threshold = 3;     // consecutive failures -> Faulted (V1)
    std::uint8_t recovery_successes = 3;  // consecutive successes -> Healthy (V1)
    std::uint32_t recovery_cooldown_ms = 5000;  // между recover() (#48 §7)
};

}
```text

### 2.3 Acquisition scheduling (модель)

- **Период ToF-слота 8 ms**: Sensing Service планирует себя bounded шагом с deadline `now + tof_slot_ms` (в окне [now, now+T_step] ✓). Внутри шага: одно ToF-чтение (RR по 4) + AS5600, если наступил его период 250 ms. Самоперепланирование - в конце шага.
- **Дедлайн-окно kernel** (`#70 §2.1`, [now, now+10 ms]) ограничивает release-time; период 8 ms укладывается. AS5600 (250 ms) НЕ выражается deadline'ом (вне окна) - период отслеживается внутри Sensing Service (`next_as5600_ms`), чтение исполняется в ближайшем 8 ms-слоте после due. Итог: 250 ms-сервис исполняется с джиттером ≤ 8 ms - в пределах бюджета (stale 1 s, запас большой).
- **Суммарный бюджет шага**: ToF read (~1.5 ms) + AS5600 read (~1 ms, 2 регистра по 2 B) ≈ 2.5-3 ms < T_step 10 ms ✓ (#48 §4). BMS-quiet guard (17 ms) - Фаза 2 (BMS-адаптер), порт уже несёт Busy.
- **Блокирующая I2C-транзакция** допустима только в шаге (bounded); «блокирующий TX запрещён» (#48 §7) относится к шинам с DMA/producer-budget - I2C-слоты bounded by design (одна транзакция ≤ слот).

## 3. Трансформации

### 3.1 Sensing-шаг (bounded, foreground)

Домен framework-free (#43: dependencies inward): `SensingService` - чистая state machine, принимает `now` и возвращает следующий срок слота; планирование (kernel::schedule с deadline now+8) - обязанность composition root (`platform/main.cpp`), не домена.

```text
sensing_step (composition root, platform/main.cpp):   # kernel::schedule(deadline now+8)
  service->step(now)                # одна bounded транзакция (ToF) + AS5600 по периоду
  kernel::schedule(sensing_step, ctx, now + service->next_step_ms())

SensingService::step(now):         # domain, чистый C++ (stdint), без Arduino/platform
  # ToF round-robin: один слот, следующее устройство
  idx = rr_index % 4                  # 0..3 -> SensorId 0..3 (ToF ID 1..4)
  rr_index++
  status = i2c->read(tof_addr(idx), 0x20, buf, 13)     # measurement block
  if status == Ok:
    dis = le32(buf+4); sig = le16(buf+10)
    record_success(idx, now, dis, sig)
  elif status == Stuck:
    schedule_recovery(now)            # cooldown-механика, WARN_I2C_RECOVERY
    record_failure(idx, now, SensorFault::BusStuck)
  else:                               # Busy | Recovered | short | unknown
    record_failure(idx, now, classify(status))          # NoAck/Stale-класс
  # AS5600: 250 ms service (период отслеживается внутри, deadline не нужен)
  if now >= next_as5600_ms:
    next_as5600_ms = now + as5600_service_ms
    status_a = i2c->read(0x36, 0x0C, buf2, 2)          # RAW ANGLE, big-endian
    raw_a = (buf2[0] << 8 | buf2[1]) & 0x0FFF
    status_b = i2c->read(0x36, 0x0E, buf2b, 2)         # ANGLE (processed)
    raw_b = (buf2b[0] << 8 | buf2b[1]) & 0x0FFF
    record_success/failure(As5600, now, raw_a, raw_b, status_a)
  refresh_freshness(now)
  m_next_step_ms = tof_slot_ms        # следующий слот через 8 ms (читается glue)
```text

- **Big-endian AS5600** (контракт владельца, прототип): `(b[0] << 8 | b[1]) & 0x0FFF`; `le16()` НЕ применять (byte-swap портит угол). RAW ANGLE @0x0C/0x0D и ANGLE @0x0E/0x0F оба валидны; первичный - RAW.
- **ToF little-endian** (V1 TOF_Sense.cpp): dis u32 @0x24, dis_status u16 @0x28, signal u16 @0x2A в блоке 0x20..0x2C (13 B).
- **Bounded step**: одна транзакция (или две для AS5600) за шаг; шаг ≤ ~3 ms < T_step. Превышение бюджета (шаг > 10 ms) - через KernelEvents::step_overrun (kernel, уже реализован #70), не отдельный механизм.

### 3.2 Freshness и health (state machine, V1 keep)

```text
record_success(idx, now, ...):
  snapshot.sample_ms = now; snapshot.raw/raw2 = ...
  consecutive_failures = 0; consecutive_successes++
  state = (consecutive_successes >= recovery_successes) ? Healthy
          : (state == Faulted ? Recovering : Healthy)
  fault = None

record_failure(idx, now, f):
  snapshot.fault = f; consecutive_failures++; consecutive_successes = 0
  state = (consecutive_failures >= fault_threshold) ? Faulted
          : (state == Starting ? Starting : Degraded)

freshness check (каждый шаг, INV-SENSING-FRESH на границе шага):
  for each sensor: if sample exists and age >= fresh_budget:
      state = (consecutive_failures >= fault_threshold) ? Faulted : Degraded
      fault = Stale (если state == Faulted)
```text

- **Stale-fault**: свежесть потеряна (age ≥ budget) при уже накопленном threshold → Faulted + fault=Stale (V1: `shouldDeclareFault` = fresh потерян + faulted-условие).
- **Recovery**: только после 3 consecutive successes (V1); состояние Recovering между Faulted и Healthy.
- **Потребление**: потребители (Safety Authority #71) читают snapshot в foreground; свежесть-классификация - обязанность Sensing Service (механизм), решение о действии - Safety Authority (#71, INV-SENSING-FRESH на SafetySlot-границе).

### 3.3 Recovery (bus stuck, obligation #14)

```text
schedule_recovery():
  if now - last_recovery_attempt_ms < recovery_cooldown_ms: return  # >=5 s
  last_recovery_attempt_ms = now
  r = i2c->recover()          # reinit + <=16 SCL + STOP; блокирующий, bounded
  if r == Recovered:
      recovery_count++
      emit WARN_I2C_RECOVERY (typed warning, registry #47)
      # все сенсоры сохраняют state (восстановление - по health per-sensor)
  else:
      fault = BusStuck (все сенсоры, чьи транзакции stuck)
```text

- Recovery-условие: транзакция вернула Stuck (HAL BUSY/TIMEOUT/ERROR). Пока cooldown активен, stuck-слоты пропускаются (bounded, без повторных попыток в цикле).
- Cooldown ≥ 5 s между попытками (#48 §7); счётчик попыток и recovery_count - наблюдаемы (evidence L4).

## 4. Зависимости и контракты

### 4.1 Dependency-матрица (внутрь, #43/#51)

| Модуль | Зависит от | НЕ зависит от |
| --- | --- | --- |
| `domain/sensing.h/.cpp` | `domain/ports.h` (I2cPort) | Arduino Core, platform/ (kernel, monotonic - glue передаёт now), адаптеров |
| `adapters/i2c_*` | Arduino Core (Wire), реализует v3::I2cPort | domain (кроме порта) |
| `platform/main.cpp` (glue) | kernel::schedule, monotonic, SensingService, I2cPort | — |

Enforcement: include-lint (#51 §5.2) - никаких Arduino/RTOS-заголовков в `domain/`; native-сборка домена без framework (build_src_filter уже разделяет).

### 4.2 Инварианты (наследуются, не пересматриваются)

| Инвариант | Источник | Проверка |
| --- | --- | --- |
| Sensing-шаг ≤ T_step (10 ms); overrun наблюдаем | #48 §4 | KernelEvents::step_overrun (#70) + L4 |
| Один ToF-чтение за слот 8 ms; RR по 4 | #48 §5 | host (каденция) + L4 |
| AS5600 service 250 ms; stale 1 s | #48 §5, #43 | host (freshness) + L4 |
| T_fresh ToF 300 ms (направленные) | #45 O3, #48 §2 | host (stale-fault) + L4 |
| Recovery ≤ 16 SCL + cooldown ≥ 5 s | #48 §7, obligation #14 | host (cooldown) + L4 |
| Freshness-классификация на границе шага (INV-SENSING-FRESH механизм) | #48 §4, #45 §6 | SafetySlot (#70) + sensing snapshot age |
| I2C - единственный владелец слотов Sensing (Фаза 1) | #43 §4 | review + L4 (нет конкурирующих транзакций) |
| Никакой динамической аллокации | #51 R1 | include-lint, review |
| ISR не вызывает sensing/I2C | #43 §3.2, R2 | review-checklist (в scope #63 новых ISR нет) |
| AS5600 big-endian (RAW @0x0C, ANGLE @0x0E) | владелец (прототип #63) | host fixture + L4 (вращение) |

## 5. Shape of code

### 5.1 Program layout

```text
domain/
  ports.h                  # + v3::I2cResult, v3::I2cPort (production-форма slice::I2cPort)
  sensing.h/.cpp           # Sensing Service: SensorId, HealthState, SensorFault,
                           # SensorSnapshot, SensingConfig, acquisition/freshness/recovery
adapters/
  i2c_bus.*                # v3::I2cPort на Wire (100 kHz, PB11/PB10 I2C2),
                           # status-классификация, recover() (reinit + <=16 SCL + cooldown)
platform/
  main.cpp                 # + wiring Sensing Service -> kernel::schedule
  sensing_schedule.h/.cpp  # glue: bounded step + re-arm (host-buildable, T16)
tests/
  test_sensing/            # acquisition scheduling, freshness, faults, recovery, snapshots
bench/
  sensing-proto/           # прототип (asset, dcb62b3) - эталон L4 read-back паттерна
  verification-runner/     # + sensing-сценарий (RAM read-back, schema v2 аддитивно)
```text

### 5.2 Public API (production-форма, без test-хуков)

```cpp
namespace v3::sensing {

// Старт acquisition (foreground, после kernel::init): сброс снапшотов и
// расписания. i2c - реализация v3::I2cPort (адаптер); владелец - glue.
void init(SensingService* svc, const SensingConfig& cfg, v3::I2cPort& i2c);

// Bounded шаг (вызывается composition root из kernel-шага): один ToF-слот +
// AS5600 по периоду, обновление снапшотов/freshness/recovery-координация.
void step(std::uint64_t now);

// Срок следующего слота (для перепланирования glue): tof_slot_ms.
std::uint32_t next_step_ms() const;

// Snapshot-запросы (foreground; потребители: Safety Authority #71,
// Observability #72). fixed-width out-параметры, без аллокаций.
bool get_snapshot(SensorId id, SensorSnapshot* out) const;
std::uint32_t recovery_count() const;

}
```text

### 5.3 Типичный call stack (target, Serving)

```text
loop() -> kernel::run() -> process_tick()
  └─ ring.run_next(now)                    # один bounded шаг за тик
     └─ sensing_tick(ctx)                  # glue (main.cpp): deadline now+8
        ├─ service->step(now)              # domain, framework-free
        │  ├─ i2c->read(tof_addr, 0x20, buf, 13)      # ~1.5 ms
        │  ├─ (если AS5600 due) i2c->read(0x36, ...)  # ~1 ms
        │  ├─ record_success/failure -> snapshot update
        │  └─ refresh_freshness(now)
        └─ kernel::schedule(sensing_tick, ctx, now + service->next_step_ms())
  ├─ (если overrun) events->step_overrun(dt_ms)
  ├─ safety->tick(now)                     # SafetySlot (#70; Ф1 stub)
  └─ watchdog.reload()
```text

## 6. Light-визуализации (псевдокод)

```cpp
// Домен: чистый state machine (framework-free). now приходит из glue.
void SensingService::step(std::uint64_t now)
{
    const std::uint8_t idx = m_rr_index++ % 4U;

    std::uint8_t buf[13] = {};
    const I2cResult r = m_i2c->read(kTofBaseAddr + idx + 1U, 0x20U, buf, 13U);
    if (r == I2cResult::Ok)
    {
        const std::uint32_t dis = le32(buf + 4U);
        const std::uint16_t sig = le16(buf + 10U);
        record_success(static_cast<SensorId>(idx), now, dis, sig);
    }
    else
    {
        record_failure(static_cast<SensorId>(idx), now, classify(r));
        if (r == I2cResult::Stuck) { schedule_recovery(now); }
    }

    if (now >= m_next_as5600_ms)
    {
        m_next_as5600_ms = now + m_cfg.as5600_service_ms;
        std::uint8_t b2[2] = {};
        const I2cResult ra = m_i2c->read(kAs5600Addr, 0x0CU, b2, 2U);
        const std::uint32_t raw_a = ra == I2cResult::Ok
            ? (static_cast<std::uint32_t>((b2[0] << 8) | b2[1]) & 0x0FFFU) : 0U;
        std::uint8_t b3[2] = {};
        const I2cResult rb = m_i2c->read(kAs5600Addr, 0x0EU, b3, 2U);
        const std::uint32_t raw_b = rb == I2cResult::Ok
            ? (static_cast<std::uint32_t>((b3[0] << 8) | b3[1]) & 0x0FFFU) : 0U;
        if (ra == I2cResult::Ok)
        {
            record_success(SensorId::As5600, now, raw_a, raw_b);
        }
        else
        {
            record_failure(SensorId::As5600, now, classify(ra));
            if (ra == I2cResult::Stuck) { schedule_recovery(now); }
        }
    }

    refresh_freshness(now);   // stale -> Degraded/Faulted (V1, §3.2)
    m_next_step_ms = m_cfg.tof_slot_ms;
}

// Composition root (platform/sensing_schedule.cpp): планирует доменный шаг в
// kernel. Дедлайн перевооружения считается от СВЕЖЕГО now ПОСЛЕ шага: длинный
// шаг (залипшая шина блокирует I2C-транзакцию) не должен делать дедлайн
// просроченным - kernel молча отклоняет out-of-window; при отклонении
// перевооружение с now+1 (в окне) - планирование не умирает молча (review
// MAJOR fix #63). Дополнительно адаптер pre-flight проверяет уровни шины
// (IDR): залипшая шина -> Stuck БЕЗ блокирующего Wire-вызова (иначе
// busy-спин до 100 ms, unbounded step).
void schedule_tick(void* ctx)
{
    auto* svc = static_cast<v3::sensing::SensingService*>(ctx);
    const std::uint64_t now = v3::monotonic::now_ms();
    svc->step(now);
    const std::uint64_t now2 = v3::monotonic::now_ms();   // свежий now
    const std::uint32_t deadline =
        static_cast<std::uint32_t>(now2) + svc->next_step_ms();
    if (v3::kernel::schedule(&schedule_tick, ctx, deadline)
            != v3::kernel::ScheduleResult::Ok)
    {
        (void)v3::kernel::schedule(&schedule_tick, ctx,
                                   static_cast<std::uint32_t>(now2) + 1u);
    }
}
```text

## 7. Тесты с call graph

### 7.1 Production call graph

```mermaid
flowchart TD
    MAIN["platform/main.cpp"] --> K["kernel::init / run"]
    K --> PT["process_tick() (foreground, ≤1 шаг/тик)"]
    PT --> RN["StepRing::run_next(now)"]
    RN --> SS["sensing::step(ctx)"]
    SS --> I2C["v3::I2cPort::read (Wire 100 kHz)"]
    SS --> FR["record_success/failure + freshness (snapshots)"]
    SS --> REC["schedule_recovery (cooldown ≥5 s, WARN_I2C_RECOVERY)"]
    SS --> RS["kernel::schedule(step, now+8) - self-reschedule"]
    PT --> SAF["safety->tick(now) (SafetySlot, Ф1 stub / #71 Ф2)"]
```text

### 7.2 Test call graph (host, deterministic)

```mermaid
flowchart TD
    G["GoogleTest (pio test -e native)"] --> TS["test_sensing"]
    TS --> FI["FakeI2cPort (scripted status/bytes, fault injection)"]
    TS --> TM["TestTimeSource (инъекция now_ms)"]
    TS --> SS["sensing::step / init / get_snapshot"]
    SS --> K["kernel::schedule (FakeKernel-hook или реальный kernel host)"]
```text

### 7.3 Тест-кейсы (L2 host; L4 - target-лега #63 acceptance)

| # | Suite | Проверяемый контракт | Метод/oracle | Среда |
| --- | --- | --- | --- | --- |
| T1 | test_sensing | ToF RR: за 4 слота прочитаны все 4 ID в порядке (8 ms каждый) | FakeI2cPort записывает адреса, 4× step(), assert order | host |
| T2 | test_sensing | Каденция: 100 шагов -> ~25 чтений/сенсор; AS5600: 1000 шагов (8 ms) -> ~32 сервиса (250 ms ± jitter слот) | счётчики в снапшотах, инъекция времени | host |
| T3 | test_sensing | Ok -> snapshot обновлён: raw/raw2, sample_ms, state Healthy | FakeI2cPort возвращает блок 0x20..0x2C (dis/signal), assert | host |
| T4 | test_sensing | Freshness: age >= T_fresh (300 ms) -> Degraded; после threshold (3) -> Faulted + fault=Stale | time advance, отсутствие успехов, assert | host |
| T5 | test_sensing | AS5600 stale 1 s -> Faulted (age >= 1000) | инъекция времени, assert | host |
| T6 | test_sensing | NoAck x3 -> Faulted (threshold, V1); 2 failures -> Degraded | FakeI2cPort scripted noack, assert | host |
| T7 | test_sensing | Recovery: Faulted -> 3 consecutive successes -> Healthy (через Recovering) | scripted fail x3, ok x3, assert state path | host |
| T7a | test_sensing | Промежуточные переходы: fail 1 -> Degraded, fail 3 -> Faulted, 1-й успех после Faulted -> Recovering | scripted fail/ok, assert states | host |
| T8 | test_sensing | Stuck -> schedule_recovery: cooldown ≥5 s (вторая попытка <5 s пропущена); recover() вызван с bounded частотой | FakeI2cPort stuck + time advance, assert calls | host |
| T9 | test_sensing | recover() -> Recovered: recovery_count++, WARN_I2C_RECOVERY типизирован | FakeI2cPort recover, assert | host |
| T10 | test_sensing | AS5600 big-endian: bytes [0x12, 0x34] -> raw 0x1234 (не 0x3412) | FakeI2cPort bytes, assert raw | host |
| T11 | test_sensing | Bounded: суммарное время шага (ToF + AS5600) ≤ T_step; >T_step -> step_overrun через kernel | FakeI2cPort с задержкой, kernel host, assert events | host |
| T12 | test_sensing | Самопланирование: шаг перепланирует себя с deadline now+8; очередь не растёт | kernel host, count ring entries | host |
| T13 | test_sensing | snapshot потребителя: get_snapshot отдаёт последний валидный (fixed-width, no alloc) | прямые assert | host |
| T14 | test_sensing | ISR-граница: i2c TU не вызывает kernel/sensing (нет вызовов из ISR-пути) | include-lint + nm-проверка символов | host |
| T15 | test_sensing | Busy (BMS-quiet): слот пропущен без изменения state; следующий Ok продолжает каденцию | FakeI2cPort busy, assert | host |
| T16 | test_sensing | Glue перевооружается после длинного шага: залипшая шина (read продвигает часы на 100 ms, busy-спин STM32duino) не убивает расписание - свежий now + перепланирование, следующий слот исполняется; recover#1 на первом Stuck, cooldown держится | kernel host + schedule_tick + FakeI2cPort(advance), assert queue/чтения | host |
| L1 | L4 | Acquisition cadence, bounded step, stale/fault/recovery transitions на реальных датчиках; raw + normalized evidence | runner sensing-сценарий (RAM read-back, §5.1) | L4 |

## 8. Vertical slice граница (#63)

Один vertical PR: Sensing Service + v3::I2cPort + I2C adapter (Wire 100 kHz, PB11/PB10) + snapshots/freshness/typed faults + host-тесты T1-T15 + runner-сценарий L4 (RAM read-back). Наблюдаемый контракт (Acceptance тикета): host-тесты защищают state/freshness/recovery; L4 runner подтверждает acquisition cadence, bounded step duration, stale/fault transitions и recovery на реальных датчиках (0x09/0x0C present -> Healthy, 0x0A/0x0B absent -> Faulted NACK, AS5600 -> Healthy) с raw и normalized evidence.

**Зависимость от стенда**: L4 acceptance требует живых датчиков (5V/GNDREF питание) И production-диагностической RAM-секции (снапшоты по pinned адресу, читаемые runner'ом) - последняя является реализацией решения владельца §0 п.3 (RAM read-back наблюдаемость, UART молчит в Фазе 1). До принятия решения runner v2-механизм (tools/readback.py, evaluate_readback_oracle, schema scenario-v2, gated observation) поставляется, но L1-сценарий `sensing-acquire` НЕ включается (структура секции фиксируется после §0). При недоступности стенда - evidence по host + прототип (деградация документируется, #52 §12).

## 9. Трассировка obligations

| Obligation (#43 §8 / #48 §11) | Закрытие в #63 |
| --- | --- |
| #2 T_check_jitter/T_arb, freshness-суб-бюджеты | Sensing snapshot age на границе шага + SafetySlot (#70); T2, T4 |
| #4 adapter duration bounds (I2C) | T11 (bounded step), L1 (L4: tof read duration) |
| #14 I2C recovery при параллельной BMS | T8, T9 (cooldown ≥5 s, ≤16 SCL); Фаза 2 - Busy-семантика T15 |
| #48 §5 каденции (ToF 8 ms RR, AS5600 250 ms) | T1, T2, L1 |
| #48 §2 T_fresh 300 ms / 1 s (pre-allocated #43) | T4, T5 (freshness/stale-fault) |
| #52 §6.3 обязательные L5 - не в этом слайсе | деградация на L4 (#52 §12), если стенд недоступен |

## 10. Assumptions / Unknowns / Confidence

- **Fact**: kernel #70 предоставляет bounded steps, SafetySlot-механизм, step_overrun-наблюдаемость и watchdog reload (реализовано и merged, PR #88).
- **Fact**: бюджеты каденций и freshness - #48 §5/§2 (ToF 8 ms/300 ms, AS5600 250 ms/1 s); recovery ≤16 SCL + cooldown ≥5 s - #48 §7, obligation #14.
- **Fact**: контракты чтения V1 (evidence index): ToF block 0x20..0x2C little-endian (dis u32 @0x24); AS5600 service 250 ms, stale 1 s, fault после 3 failures, recovery после 3 successes.
- **Fact**: AS5600 RAW ANGLE @0x0C/0x0D и ANGLE @0x0E/0x0F - оба валидные 12-bit big-endian слова (контракт владельца); le16() не применять. [Подтверждается live-run прототипа: вращение магнита]
- **Assumption**: PB11/PB10 = I2C2 (netlist #73; ядро STM32duino pinmap подтверждает PB11=I2C2_SDA, PB10=I2C2_SCL). Прототип-прогон подтвердил GPIO-инициализацию AF4/open-drain (readback 2026-08-13).
- **Unknown (блокер live-run)**: 5V/GNDREF (изолированная) сторона стенда обесточена на дату прогона 2026-08-13 (все I2C-транзакции Stuck, SCL/SDA в 0 при pull-up; IDR GPIOB 0x0000DF13); владелец уточняет физическое состояние. L1-сценарий исполняется после восстановления питания; до этого - host + прототип evidence.
- **Unknown**: фактическая длительность ToF-чтения на живом датчике (прототип мерил только NACK-путь, 4-5 µs; норматив ~1.2-1.5 ms, #48 §4 code-derived) - подтверждается L1 (tof read duration, bounded step).
- **Assumption**: Wire без DMA блокирует только на время транзакции (bounded, ≤ слот) - допустимо в шаге (обоснование §2.3); альтернатива (DMA-I2C) - отдельное решение, если измерение L1 покажет превышение бюджета.
- **Unknown**: wire-маппинг typed faults/warnings (registry #47 §16.4 в разработке) - в #63 доменные enum; wire-коды добавляются при готовности реестра (аддитивно, без изменения контракта порта).
- **Fact/отклонение**: recovery-механика адаптера - reinit + recoverBus ядра STM32duino (до 20 SCL-импульсов, останавливается при освобождении шины); норматив #48 §7/obligation #14 - ≤ 16 импульсов. Буквальное расхождение (20 max у ядра) принято: механика идентична (импульсы до отпускания), собственный генератор ≤16 импульсов - избыточность для Фазы 1; пересмотр при L4-замере recovery.
- **NIT (принят)**: age_ms - uint32; возраст сэмпла > ~49.7 суток непрерывной работы с мёртвым сенсором оборачивается (stale не сработает). Недостижимо практически: RR-опрос каждые 8 ms, стрик-фейлы Fault-ят задолго (threshold 3). Дизайн-бюджет - uint32 age (как в V1).
- **Confidence**: высокая по state/freshness/recovery (V1 keep, host-тестируемо); средняя по L4-evidence (зависит от восстановления питания стенда и HITL-решения §0 по диагностической секции).

## 11. Условия пересмотра

- Измеренный ToF-чтение > слот/шаг бюджета на L1 - пересмотр планирования (DMA-I2C или разнесение AS5600-чтений по отдельным слотам).
- L1 показывает jitter каденции > допустимого (AS5600 пропускается > 1 s) - пересмотр расписания (выделенный AS5600-слот).
- Появление BMS-адаптера (Фаза 2) - активация Busy-семантики и BMS-quiet guard в расписании (#48 §7).
- Wire-реестр #47 §16.4 фиксирует коды - добавление маппинга typed faults/warnings (аддитивно).
- Изменение адресации/контракта датчиков на стенде - rebaseline bench record (#73).

## 12. Ссылки

- Тикет #63 (этот), #58 (карта), #70 (kernel, PR #88), #85 (design-шаблон), #73 (L4 стенд, bench record), #60/#65 (verification runner), #68 (gate 1→2), #71 (Safety Authority, потребитель snapshot), #72 (Observability).
- `docs/quality-attributes-and-budgets-v3.md` (#48), `docs/software-architecture-boundaries-v3.md` (#43), `docs/safety-model-v3.md` (#45), `docs/implementation-plan-v3.md` (Фаза 1), `docs/verification-strategy-v3.md` (#52), `docs/external-semantic-transport-contracts-v3.md` (#47 §16.4).
- V1: `docs/research/v1-system-evidence-index.md` (TOF_Sense/As5600HealthMonitor/TofHealthMonitor/TofBusMonitor), `C:\Projects\Shuttle\ShuttleController\Cntrl_V2\` (референс-код).
- Прототип: `bench/sensing-proto/` (asset, коммит dcb62b3) - контракты чтения, каденции, RAM-наблюдаемость.
