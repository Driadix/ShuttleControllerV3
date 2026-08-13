# Sensing acquisition prototype (ticket #63)

Throwaway-прототип sensing-слайса [«Реализовать sensing-слайс ToF и AS5600»](https://github.com/Driadix/ShuttleControllerV3/issues/63). Отвечает на дизайн-вопросы, которые блокируют design-док слайса, на реальном L4-стенде (`docs/l4-sensor-bench-v3.md`):

1. Фактический контракт чтения ToF: contiguous block `0x20..0x2C` (13 B, little-endian: system_time u32, dis u32, dis_status u16, signal_strength u16, range_precision u8) на адресах `0x09..0x0C`.
2. Фактический контракт чтения AS5600: **RAW ANGLE @0x0C/0x0D** и **ANGLE @0x0E/0x0F** - оба валидны, 12-bit **big-endian** слова `(b[0] << 8 | b[1]) & 0x0FFF`. Оба захватываются как независимые наблюдения; владелец вращает магнит, диф показывает, какой регистр отслеживает поворот. `le16()` для AS5600 НЕ применять (byte-swap портит угол).
3. Каденция acquisition (issue #48 section 5, V1 keep): ToF - один 8 ms слот на чтение, round-robin по 4 сенсорам; AS5600 - каждые 250 ms.
4. Длительность одного ToF-чтения (бюджеты: слот 8 ms, шаг 10 ms; issue #48 sections 4/7).
5. Fresh/Stale/Fault классификация на реальном состоянии стенда: `0x09`/`0x0C` присутствуют -> Healthy; `0x0A`/`0x0B` физически отсутствуют -> NACK -> Faulted (3 подряд); AS5600 -> Healthy.

Это **не production-код**: production I2C-адаптер, Sensing Service, порты, snapshot-контракт и runner-сценарий - вертикальный PR слайса.

## Наблюдаемость

UART в frozen baseline отключён, поэтому диагностика идёт через RAM (тот же паттерн, что bring-up #61): диагностическая структура пишется в pinned RAM `0x20011000` (секция `.bram_sensing`), вырезается из flash-образа (objcopy) и читается через OpenOCD `mdw`.

Структура (52 слова): `magic/version/uptime_ms/loop_count/i2c_reads/i2c_fails/last_tof_slot_ms/last_as5600_ms/tof_read_us_max/tof_read_us_total/tof_read_count/reserved` + 5 сенсоров по 8 слов: `raw, raw2, age_ms, state, samples_ok, samples_fail, last_status, last_sample_ms`. Роли: 0 = ToF ID1 `0x09` (channel reverse), 1 = ToF ID2 `0x0A` (channel forward), 2 = ToF ID3 `0x0B` (pallet reverse), 3 = ToF ID4 `0x0C` (pallet forward), 4 = AS5600 `0x36`. State: 0 Starting, 1 Healthy, 2 Degraded, 3 Faulted, 4 Recovering. Status: 0 ok, 1 noack, 2 short, 3 unknown.

## Команды

Требования: `.venv-pio312` (Python 3.11), PlatformIO Core, OpenOCD из пакетов PIO, ST-Link на XT21, датчики по bench record #73.

```text
# 1. Подпись владельца per-run (ОБЯЗАТЕЛЬНА для run; физическое взаимодействие,
#    жёсткий gate, #60 §0.3):
#    {schemaVersion: 1, signed: true, owner: "Driadix", at: <iso>, items: [{... confirmed: true}]}
#    (пример: bench/sensing-proto/checklist.example.json)

# 2. Read-only диагностика (без gate):
.venv-pio312/Scripts/python.exe bench/sensing-proto/check_sensing.py probe
.venv-pio312/Scripts/python.exe bench/sensing-proto/check_sensing.py readback

# 3. Полный прогон: checklist -> flash -> наблюдение -> 2 снапшота -> вердикт -> evidence:
.venv-pio312/Scripts/python.exe bench/sensing-proto/check_sensing.py run \
    --duration 10 --checklist checklist.json

# 4. Офф-бенч демо вердиктов на фикстурах (без железа):
.venv-pio312/Scripts/python.exe bench/sensing-proto/check_sensing.py verdict \
    --from-a bench/sensing-proto/fixtures/snap-a-pass.mdw \
    --from-b bench/sensing-proto/fixtures/snap-b-pass.mdw --window 5     # PASS, exit 0
.venv-pio312/Scripts/python.exe bench/sensing-proto/check_sensing.py verdict \
    --from-a bench/sensing-proto/fixtures/snap-a-pass.mdw \
    --from-b bench/sensing-proto/fixtures/snap-b-fail-cadence.mdw --window 5  # FAIL, exit 1
```

Exit-коды (конвенция runner #60): 0 PASS, 1 FAIL, 3 INCOMPLETE (отказ до side effects - нет/невалидный checklist, невалидный снапшот), 4 tool error.

## Live-run процедура (split владелец/агент)

| Шаг | Исполнитель |
| --- | --- |
| Физическое подключение (ST-Link XT21, датчики на разъёмах), energizing, подпись checklist | владелец |
| Probe/flash/readback/verdict/evidence | агент (runner) |
| Вращение магнита AS5600 во время наблюдения (подтверждение контракта угла: какой регистр - RAW ANGLE / ANGLE - отслеживает поворот) | владелец (по запросу) |

Вердикт PASS требует: каденция присутствующих сенсоров >= 60% идеальной (ToF ~31/s, AS5600 ~4/s), состояния Healthy для `0x09`/`0x0C`/AS5600 и Faulted для `0x0A`/`0x0B`, свежесть age < budget (ToF 300 ms, AS5600 1 s), одно ToF-чтение < 8 ms.

## Тесты (host, без железа)

```text
.venv-pio312/Scripts/python.exe -m unittest discover -s bench/sensing-proto/tests -t .
```

Кейсы: parse mdw, snapshot valid/bad magic/short, каденция pass/fail, states present/absent/starting, freshness fresh/stale, бюджет чтения, full verdict pass/fail.

## Прототип -> дизайн-док

Результаты прототипа (контракты чтения, измеренные длительности, наблюдаемая каденция/состояния, RAM-наблюдаемость) входят в `docs/sensing-slice-design-v3.md` (тикет #63) как факты раздела 10; решения владельца фиксируются в §0.
