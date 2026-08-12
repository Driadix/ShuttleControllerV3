# Границы и владение архитектуры V3 (Software Architecture)

Статус: утверждено по issue [«Определить архитектурные границы и ownership foundation V3»](https://github.com/Driadix/ShuttleControllerV3/issues/43).

Этот документ задаёт архитектурные границы, single-writer ownership и правила прохождения intents, observations и outcomes через границы firmware-платформы V3. Он является входом в logical item `Software Architecture` нормативного пакета (issue 8) и опирается на решение «Выбрать execution architecture V3 по Gates + evidence» (issue 10): cooperative scheduler с bounded run-to-completion steps, один scheduling domain, host-deterministic core.

Термины - канонические из `CONTEXT.md`: Safety Authority, Manual Control Session, операция/субоперация/примитив, транспортный профиль, запрос операции, outcome.

## 1. Форма границ: ports-and-adapters

Первичная декомпозиция - hexagonal: host-deterministic domain core (без Arduino/RTOS зависимостей) и кольцо bounded адаптеров, реализующих порты. Направление зависимостей строго внутрь: domain не знает адаптеров, адаптеры реализуют доменные порты. Enforcement - include-дисциплина и раздельная сборка domain-ядра.

Порты - по классам адаптеров, а не по устройствам. Обоснование: gates issue 10 (normalized hardware boundary, host-deterministic core) и acceptance тикета 43 (границы исполнимы и проверяемы на host).

## 2. Инвентарь компонентов

### Domain core (8 компонентов)

| Компонент | Одна обязанность | Прямой ответ на V1-дефект |
| --- | --- | --- |
| Semantic Contract & Admission | валидация запросов (парсинг-независимо), authority-роли, epoch fencing, idempotency, admission, создание операций; policy очередей и overload | admission, размазанный по processPacket и run_Cmd |
| Operation Runtime | lifecycle операций (дерево root/suboperation), bounded step execution, composition, outcomes; исполнение safety-операций как внутренних корневых | синхронные длинные операции внутри run_Cmd |
| Safety Authority | policy: precedence, единственная arbitration boundary, fault/warning model, safe states, watchdog health aggregation, stall-детекция, obstacle-классификация, авторизация эвакуации, мониторинг manual lease | SystemYield как safety-ядро |
| Sensing Service | единственный писатель наблюдений (ToF, AS5600, паллеты, концевики, bumper events), freshness/staleness, health-состояния, одометрия (позиция), I2C-слот-расписание | get_Distance: 5 обязанностей |
| Actuator Controller | владение арбитрированным actuator state (commanded), единственный производитель CAN-кадров 100/101 | motor_Speed с sensing внутри ramp |
| Config & Profile | конфигурация, профили 800/1000/1200, калибровки, runtime-состояние, валидация, policy сохранения, восстановление калибровок | live/persisted shadow |
| Observability Producer | structured events/telemetry/logs/traces как данные; единственный владелец счётчиков и статистики | inline-инкременты счётчиков |
| Manual Session | состояние сессии (lease, hold-to-run, heartbeat) | CMD_MANUAL_MODE в одном статусе с операциями |

В cooperative модели компоненты - модули с bounded step-функциями, а не потоки: стоимость организационная, не вычислительная.

### Adapter ring (4 класса)

- **HAL**: CAN, UART display, UART radio (E22), I2C (ToF, AS5600), GPIO (датчики + индикаторы), Flash (journal), RTC, Watchdog, ADC. Владение внутри HAL - per-peripheral: одна периферия - один владелец.
- **Transport**: display profile, radio profile - проекция semantic contract на канал; ACK/reply-решения принимает Semantic Contract, адаптер только кодирует.
- **Persistence**: flash journal (ротация, CRC16, атомарность, wear).
- **Observability Sink**: bounded очереди четырёх классов, TX-планирование, механическое enforcement overload.

## 3. Потоки через границы

### 3.1 Intents и arbitration

Все actuator intents - auto (Operation Runtime), manual (Manual Session), safety (Safety Authority) - проходят через единственную arbitration воронку внутри Safety Authority. Наружу выходит единственный текущий intent; Actuator Controller исполняет только его и не имеет собственной policy.

- Intent = {цель: velocity setpoint | лифтер | stop с профилем | force-stop, источник, приоритет, метаданные ожидаемых точек останова}.
- В штатном режиме воронка - pass-through единственной активной activity (одна Exclusive Control Activity, issue 2); при fault/stop/lease-loss Safety заменяет поток на safety intent.
- Stop-профиль (плавный ramp vs немедленный нулевой кадр) выбирает policy arbitration, а не статус операции.
- Safety-операции (эвакуация, low-battery реакция) - авторизованные внутренние корневые операции, исполняемые Operation Runtime; их intents идут той же воронкой.
- Lease-мониторинг: Manual Session - единственный писатель lease-счётчиков и expiry-события; Safety Authority - единственный потребитель, превращающий expiry в stop-intent через воронку.
- Метаданные ожидаемых точек останова от операции позволяют Safety Authority классифицировать «ожидаемая близость» vs «препятствие» по snapshot'ам Sensing (без отдельного канала управления защитой).

### 3.2 Observations

- Sensing Service - единственный писатель всех наблюдений (raw и derived). Потребители читают snapshot'ы; прямого I/O из операций нет.
- Snapshot обязан различать commanded (целевое состояние: motorStart/Reverse, lifterUp) и observed (измеренное: позиция, концевики) - смешение давало V1-флаг lifterUp vs физический CHANNEL.
- Внеочередные слоты опроса по запросу операций имеют приоритет/aging, чтобы не голодать на занятой шине.
- Одометрия (позиция) - derived observation, владелец Sensing Service: интеграция реального движения по AS5600 + калибровка + профиль.
- Freshness-окна (ToF 300 ms-класс, AS5600 1 s-класс) - аллоцированные суб-бюджеты единой event→safe-output цепочки; учёт цепочки целиком (детекция → arbitration → эмиссия CAN) ведёт Safety Authority; числовые значения - item 9. Это зависимость item 10 → item 9: freshness-суб-бюджеты пред-аллоцированы архитектурным решением и не проектируются независимо.
- ISR (bumper edges) пишет только в свой bounded ring в GPIO adapter; классификация и fault-latch - в Sensing Service / Safety Authority. ISR не исполняет policy.

### 3.3 Outcomes

- Outcomes - собственность Operation Runtime: стабильные typed результаты с диагностическим context; admission-отказы - свойство запроса, не экземпляра (semantic contract).

## 4. Single-writer условия по ресурсам

### CAN (Q6 + поправки)

- Один CAN adapter - единственный владелец периферии: bounded TX, bounded RX-drain с бюджетом кадров за тик, dispatch O(1) по ID.
- Actuator Controller - единственный производитель кадров 100/101; ток лифтера (2405) - RX-потребитель Actuator Controller; политика RX-переполнения при потоке 2405 - отдельная (drop + событие по правилам раздела 6).
- Force-stop: отдельный extended ID минимального значения на шине (ниже 100/101/2405 и любых статусных ID приводов) + выделенный TX mailbox bxCAN с наивысшим приоритетом; содержимое кадра фиксируется в контракте адаптера. Force-stop не стоит в очереди control-класса.
- Unknown (обязанность item 5 Safety & Health): собственная аппаратная защита приводов при отказе CAN-шины - fail-safe не должен зависеть только от CAN TX.

### UART / transport

- Transport адаптеры владеют UART: byte budget за тик (фикс V1 unbounded drain), bounded parsing, никогда не блокировать: блокирующий TX (HardwareSerial при полном буфере) исключается DMA-TX или producer-budget (Producer не кладёт больше, чем Sink может слить). Доказательство неблокирующего TX - обязательный пункт proving slice.

### I2C

- Один I2C bus adapter (единственный владелец Wire, виртуальные каналы на устройство). Sensing Service владеет I2C-слот-расписанием и потребляет «bus-busy» события от BMS adapter (V1: I2C молчит во время BMS TX и 5 ms после); radio audit ограничивает I2C-бюджет тем же механизмом. Recovery: адаптер - механика (reinit, SCL-импульсы, cooldown), Sensing - health state machine.

### Actuator output

- Actuator Controller - единственный писатель commanded-состояния и единственный источник кадров 100/101; вход - только арбитрированный intent.

### Mutable state

- Распределённое владение: состояние живёт у своего писателя (раздел 2). Кросс-доступ - только read-only snapshot'ы; прямых записей в чужое состояние нет; locks не нужны (один поток исполнения).
- Счётчики/статистика: компоненты и адаптеры эмитят события, счётчики ведёт Observability Producer (адаптер не инкрементирует сам).

### Persistence (Q8 + поправки)

- Flash adapter - единственный владелец периферии: journal-механика, bounded контракт, построенный от худшего случая: (а) программа 512 B - десятки мс, (б) стирание сектора 128 КБ - порядок 0.5-2 s как атомарное блокирующее окно (не разбивается на bounded шаги). Точные числа - из datasheet и измерения.
- Config & Profile - единственный писатель конфигурационного состояния и policy сохранения (когда/отложить/retry; явные vs авто-сохранения; восстановление encoder-калибровки после reboot - явный пункт, в V1 закомментировано). Одно авторитетное состояние; journal - его durable-копия; двойного shadow нет.
- Quiescence-правило: движение остановлено и подтверждено по snapshot'ам Actuator/Safety И окно стирания < минимального safety-дедлайна И < окна watchdog. Enforcement сериализации - execution core, решение «сейчас stationary и безопасно» - Config & Profile по snapshot'ам (одна фраза: policy у Config & Profile, enforcement у execution core).
- Развилка реализации: RAM-exec драйвера vs принятие окна - по измерению на proving slice (dual-bank на F405RG недоступен).
- Backup-SRAM persistent stats - владелец Persistence adapter, писатели через запросы Observability Producer.

### Watchdog

- Reload - обязанность execution core на каждой границе bounded-шага и в idle-loop; health-агрегация - Safety Authority; hardware - HAL watchdog adapter.
- Формула окна (item 9): окно = max(худший bounded шаг, flash-окно, ISR-время) + margin. Единица V1-аргумента watchdog API подтверждена (IWatchdog, µs; V1 `10000000` = 10 s, #45 §7.1); числовое окно — item 9 (#48): 10 s.

### Время

- Monotonic clock - порт execution core (tick source); wall clock - HAL RTC adapter; синхронизация - Service-класс, команда типа MSG_SET_DATETIME (внешний backend ставит NTP-время). Все доменные таймауты (lease, freshness, no-progress) считаются от monotonic, а не от RTC; NTP-скачок не ломает monotonic-семантику (validation obligation).

## 5. Стартап, update, safety-границы

- **Стартап**: владелец - execution core. Порядок: адаптеры → Sensing (grace-окно, V1-базелайн 1 s) → Ready-статус Safety Authority → движение разрешено. Восстановление конфигурации и калибровок - Config & Profile.
- **Update lifecycle**: владелец - Update Authority + Persistence adapter (staging, запись, power-loss во время обновления, recovery от битого update). Power-cut во время update - обязательный тест proving slice.
- **Stall-детекция**: владелец правил - Safety Authority: no-progress окно по snapshot'ам Sensing (позиция) + Actuator (commanded motion), fault-классификация по правилам item 5.
- **Obstacle**: классификация «ожидаемая близость vs препятствие» - Safety Authority по snapshot'ам Sensing + метаданным ожидаемых точек останова (раздел 3.1); детали - item 5.

## 6. Queue-class и overload policy

Единый фреймворк (never-block), policy - Semantic Contract, числа - item 9, enforcement - механический в adapters/sink.

- Входящие классы: Control (запросы операций/queries/subscriptions), Service (provisioning/config/calibration/diagnostics), Update (firmware update). Фиксированные бюджеты: байт/кадров за тик, глубина.
- Исходящие классы: telemetry, events, logs, traces - bounded очереди.
- Overload по роли класса:
  - telemetry (streaming): drop-oldest, приоритет свежести;
  - events/logs (диагностические): гарантированный резерв ёмкости, при переполнении - drop-newest (не уничтожать ранние свидетельства fault);
  - request/response (Control/Service, новые запросы Update): reject на admission с явным outcome;
  - Update с in-progress транзакцией: резерв ёмкости, pause-семантика, reject только для новых запросов.
- Каждый drop/reject: событие + счётчик (счётчик ведёт Observability Producer, адаптер эмитит событие).
- Safety/control - резерв ёмкости; force-stop вне очередей (раздел 4, CAN).
- Transport: byte budget за тик; блокирующий TX запрещён (раздел 4, UART).

## 7. Decision matrix (спорные границы)

| Граница | Решение | Отклонено | Основание |
| --- | --- | --- | --- |
| Форма швов | ports-and-adapters, deps внутрь | 3-layer; без шва | gates issue 10 |
| Инвентарь | 8 domain + 4 класса адаптеров | State Store; Orchestrator | каждый компонент = V1-дефект |
| Arbitration | единая воронка в Safety Authority | 2 порта; нейтральный arbiter | motor_Stop: 10+ call sites, 3 семантики |
| I2C / наблюдения | один bus adapter; Sensing - единственный писатель | per-device; прямые чтения | get_Distance: 5 обязанностей |
| Freshness | суб-бюджет event→safe-output; учёт - Safety Authority | отсутствие бюджета (= V1) | коллапс цепочки в SystemYield |
| Mutable state | распределённое владение, snapshot-чтение | центральный Store; глобалы | status: 4 смысла |
| Одометрия | Sensing Service (derived observation) | Actuator Controller | два писателя наблюдений |
| Счётчики | Observability Producer, события | inline-инкременты | три писателя в глобалы |
| CAN | один adapter; Actuator - единственный producer 100/101; force-stop = min extended ID + выделенный mailbox | per-device owners | unbounded drains |
| BMS | HAL adapter, наблюдения → Sensing | - | правило observations |
| Очереди/overload | 3 входящих + 4 исходящих класса; never-block; per-class overload; переполнения наблюдаемы | per-class ad-hoc; глобальная очередь; backpressure | static queues 3 classes; bounded steps |
| Persistence | Flash adapter + Config & Profile policy; quiescence; одно состояние | Persistence Service; V1-стиль | live/persisted shadow; blocking save |
| Flash stall | Assumption/Unknown + первоклассная obligation | (как Fact - отклонено) | маркировка evidence |
| Watchdog | reload: execution core; health: Safety Authority | reload из Safety | isolated safety path |
| Время | Time-порт; monotonic core; RTC adapter; sync Service-классом | время из парсера | MSG_SET_DATETIME в processPacket |
| Manual Session | отдельный domain-компонент; воронка; lease: Session пишет, Safety превращает в stop | внутри Operation Runtime | 4-смысловой status |
| Stall-детекция | Safety Authority (no-progress по snapshot'ам) | Actuator Controller (дубль) | V1: blink_Work в цикле движения |
| Obstacle | Safety Authority классифицирует по snapshot'ам + метаданным точек останова | доменная логика молча отключает защиту | общие ToF-данные домена и safety |
| Стартап | execution core + Ready-статус Safety | без владельца | startup grace V1 |
| Update lifecycle | Update Authority + Persistence adapter | без владельца | единственный путь к «кирпичу» |
| Перекрёстные шины | Sensing владеет I2C-расписанием, bus-busy от BMS | SystemYield-координация | V1: I2C молчит при BMS TX |

## 8. Validation obligations (proving slice; бюджеты - item 9)

1. Event→safe-output сквозно: sensing freshness + arbitration + CAN emission.
2. Freshness суб-бюджеты пред-аллоцированы (зависимость item 10 → item 9).
3. Flash: stall duration + ISR entry latency (включая вектор-выборку) + force-stop эмиссия в окне erase/program; выбор RAM-exec vs принятие окна по измерению.
4. Adapter duration bounds: CAN/I2C/UART/Flash.
5. Watchdog под combined load; окно по формуле max(шаг, flash-окно, ISR) + margin.
6. Manual lease timeout → safe stop latency.
7. Переполнения очередей под load; счётчики наблюдаемы.
8. Bounded steps cooperative.
9. NTP-скачок времени не ломает monotonic-семантику (lease/freshness/таймауты).
10. RAM/stack/link-map: per-function stack, ISR allowance, heap policy, high-water, CPU margin (синхронизация с evidence-списком issue 10).
11. Power-cut: во время save, во время update, mid-operation.
12. Блокирующий TX: Serial write при полном буфере не блокирует (DMA или producer-budget); log-storm тест.
13. CAN-драйвер как отдельный пункт: dual-class TX (mailbox priority + минимальный ID), RX-переполнение, flood-тест.
14. I2C-recovery на stuck-bus при параллельной BMS-транзакции.
15. Radio AUX-hang и E22 mode-settle внутри бюджетов тика.

## 9. Assumptions и Unknowns

- **Assumption:** erase/program flash на STM32F405 останавливает prefetch из того же банка и задерживает вектор-выборку ISR (поведение кремния по reference manual; конфигурация этой PCB не проверена).
- **Unknown:** точная длительность программа 512 B и стирания 128 КБ сектора на этой плате; bank mode/ART; ISR entry latency в окне; переживает ли bumper-ring capture и CAN TX окно.
- **Unknown:** собственная аппаратная защита приводов при отказе CAN-шины (обязанность item 5).
- **Unknown:** единица аргумента watchdog API Arduino core (V1: `10000000`).
- **Fact:** stm32duino HardwareSerial TX блокируется при полном буфере (64 B) - требует DMA-TX или producer-budget.
- **Fact:** в V1 восстановление encoder-калибровки из flash закомментировано - в V3 это явный пункт Config & Profile.
- Baseline-числа V1 (ToF каденция 8 ms, freshness 300 ms, control tick 50 ms) реалистичны как старт, но item 9 пересчитывает их от safety-требований hazard analysis; 300 ms окно при типовой скорости шаттла означает 0.3-0.5 м движения «вслепую» - приемлемость решает item 5.

## 10. Ссылки

- Тикет «Определить архитектурные границы и ownership foundation V3» (#43) и его resolution-комментарии, включая независимое экспертное ревью.
- Тикет «Выбрать execution architecture V3 по Gates + evidence» (#10).
- [Системный индекс свидетельств V1](./research/v1-system-evidence-index.md).
- `CONTEXT.md` - канонические термины.
- Тикет «Определить структуру нормативного пакета и verification gates» (#8) - logical item 10.
