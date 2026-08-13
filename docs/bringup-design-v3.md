# Дизайн bring-up evidence слайса тикета #61

## 0. Решения (подтверждены владельцем 2026-08-13)

- [x] D1. CAN ping/loopback: **D1a** - минимальный диагностический loopback-прогон на CAN-периферии.
- [x] D2. Watchdog arm: **D2b** (пересмотрено после advisory) - watchdog fire experiment: диагностическая прошивка с IWatchdog.begin(2 s) без reload, ожидание self-reset, чтение RCC_CSR.IWDGRSTF. Доказывает полный путь arm+fire+reset. D2a (чтение IWDG_CNT) неисполним на F405 - счётчика нет (RM0090 §22.3).
- [x] D3. Evidence record: `docs/bring-up-report-v3.md` (tracked) + runner evidence bundle в `out/` (untracked), ссылки на канонический bench record #73.
- [x] D4. Sensor-bus presence: reuse evidence из bench record #73 (0x09/0x0C/0x36 PASS, 2026-08-13).
- [x] D5. Pin contract (Semantic, подтверждено владельцем): **D5a** - stock board def остаётся frozen; pin contract фиксируется ЯВНОЙ инициализацией периферии в Phase-1 адаптерах на netlist-пинах (TwoWire PB10/PB11, Serial1=USART1 PA9/PA10, USART6 PC6/PC7 для E22, CAN1 AF9 PB8/PB9). Отклонение дефолтов board def документируется в bring-up report. Board JSON override и fork ядра не вводятся.

## 1. Вопрос и acceptance

**Вопрос**: проходит ли ControllerV6 Phase 0 bring-up под production baseline и frozen аппаратным контрактом?

**Acceptance (#61)**: опубликован bring-up evidence record с firmware SHA, toolchain и bench identity; все критерии перехода 0 -> 1 из `docs/implementation-plan-v3.md` имеют pass/fail; принятый board definition и pin contract однозначны.

**Критерии 0 -> 1** (implementation-plan §5, Фаза 0 §2): плата boot-ится под frozen toolchain; пины сверены; startup-to-Ready не измеряется (нет полной прошивки); переход: исправный мониторинг + CAN-пинг + watchdog-арм.

## 2. Текущее состояние (факты, проверено 2026-08-13)

| Факт | Значение | Источник |
| --- | --- | --- |
| Bench identity | STM32F405RG, idcode `0x100f6413`, UID `002900363033470336363131` | live probe, bench record #73 |
| ST-Link | V2, XT21, probe PASS (halt/resume, PC меняется) | bench record #73 |
| UART-путь | COM9 relay-дисплей 230400 8E1, port открывается | runner detect, live |
| I2C-шина | 0x09/0x0C (ToF) + 0x36 (AS5600) отвечают, VALID clear | bench record #73 (прошивка V1-эры) |
| Прошивка на main | v3::kernel (#70): tim2_clock, iwdg_watchdog, reset_cause; KernelEvents/SafetySlot - no-op стабы | platform/main.cpp |
| UART в прошивке | ВЫКЛЮЧЕН frozen build flags (`NO_HW_SERIAL`, `HAL_UART_MODULE_DISABLED`, #51 §4) | platformio.ini |
| CAN в прошивке | `HAL_CAN_MODULE_ENABLED`, драйвера НЕТ (HAL-модуль включён, код отсутствует) | platformio.ini, adapters/ |
| Watchdog | IWDG 10 s nominal, армится `IWatchdog.begin()` в init, reload каждый тик | adapters/iwdg_watchdog.cpp |

Вывод: из 0 -> 1 критериев сейчас исполнимы без новых прошивок - boot (flash-boot-smoke), мониторинг (host pipeline), watchdog-арм (регистры). CAN-пинг требует либо минимального диагностического кода, либо отсрочки.

## 3. Evidence-матрица (критерий -> метод -> инструмент -> артефакт)

| # | Критерий | Метод | Инструмент | Evidence-артефакт | Ожидание |
| --- | --- | --- | --- | --- | --- |
| E1 | Boot под frozen toolchain | flash-boot-smoke прогон (checklist-подпись владельца) | runner `run` (checklist + flash-boot-smoke.json) | `out/` result JSON: verdict PASS, firmware {gitSha, artifactSha256}, toolchain {platformio, platform, core}, board {part, idcode, uid} | PASS |
| E2 | Мониторинг (host pipeline) | detect + uart-probe прогон; MCU молчит (UART выключен, #60 §0.4) | runner `detect`, runner `run` uart-probe.json | result JSON: uart port open; TIMEOUT по кадрам = ожидаемое поведение (задокументировано) | порт открыт; TIMEOUT задокументирован как ожидаемый до #72 |
| E3 | Watchdog arm | IWDG fire experiment: RAM-маркер `[kMagic, ARMED]` -> self-reset через ~2 s (IWDG, no reload) -> boot `[kMagic, PASSED]` + keep-alive; RCC_CSR.IWDGRSTF bit 29 = 1 | `bringup/watchdog_fire` (pio env) + OpenOCD `mdw` маркера (0x2000F000) и RCC_CSR (0x40023874) | bring-up report: маркер + RCC_CSR | PASS: marker PASSED И bit 29 = 1 (fire experiment, D2b; IWDG_CNT не существует на F405, RM0090 §22.3) |
| E4 | CAN ping/loopback | минимальный диагностический прогон: CAN1 init 500k loopback, N тест-кадров, RX-счётчик в RAM; результат читается OpenOCD (UART выключен) | `bringup/can_loopback` (pio env) + OpenOCD RAM read | bring-up report: RX/TX счётчики | TX == RX == N (внутренний loopback; трансивер/шина - граница до #62) |
| E5 | Пины сверены | сравнение board def `genericSTM32F405RG` (STM32duino default mapping, проверено по установленному ядру) с frozen netlist (bench record #73 §Аппаратный контракт) | документация + сборка под def | bring-up report: таблица pin-контракт с evidence-ссылками | **РАСХОЖДЕНИЕ ПОДТВЕРЖДЕНО** на уровне дефолтов (см. §4) -> Semantic-решение D5, board def не «молча» подгоняется |
| E6 | Sensor-bus presence | reuse evidence bench record #73 (0x09/0x0C/0x36 отвечают) | - | ссылка в bring-up report | PASS (зафиксировано #73) |
| E7 | Startup-to-Ready | НЕ измеряется (нет полной прошивки, план §2 Фаза 0) | - | запись в report: N/A по плану | N/A |

## 4. Pin-контракт (E5): таблица сверки (факты проверены по установленному ядру 2026-08-13)

Источники: `framework-arduinoststm32/variants/STM32F4xx/F405RGT_F415RGT/variant_generic.h`, `PeripheralPins.c`, V1-индекс `docs/research/v1-system-evidence-index.md:47,49,139`.

| Периферия (netlist #73) | Пины | Capability в PeripheralPins | Default в board def | Совпадение default |
| --- | --- | --- | --- | --- |
| UART display/network_bridge | PA10 RX / PA9 TX (USART1) | USART1 PA9/PA10 AF7 (строки 183, 198) | `Serial` = **UART4** PA0/PA1 (`SERIAL_UART_INSTANCE 4`, строки 170-181) | **НЕТ**: дефолтный `Serial` не идёт на netlist-пины; работает только явный `Serial1` (USART1) |
| UART BMS RS485 | PA3 RX / PA2 TX (USART2) | USART2 PA2/PA3 AF7 (строки 182, 197) | Serial2 = USART2 PA2/PA3 | да |
| UART LoRa E22 | PC7 RX / PC6 TX | **USART6** PC6/PC7 AF8 (строки 186, 201) | Serial3 = USART3 (PB10/PC10 TX, PB11/PC11 RX) | **НЕТ**: PC6/PC7 на F405 - USART6, не USART3; V1-индекс:49 называет канал `Serial3` PC7/PC6 - именование V1 не соответствует hardware (зафиксировать, решить в D5) |
| I2C ToF + AS5600 | PB11 SDA / PB10 SCL (I2C2) | I2C2 PB11/PB10 AF4 (строки 96, 107) | `Wire` = **I2C1** PB7/PB6 (`PIN_WIRE_SDA/SCL` PB7/PB6, строки 158-161) | **НЕТ**: дефолтный `Wire` не достигает датчиков; V1 фактически использовал `Wire` на PB11/PB10 (V1-индекс:47) - stock def V1-поведение НЕ воспроизводит |
| CAN | PB8/PB9 (CAN1) | CAN1 PB8/PB9 AF9 (строки 283, 293) | первое вхождение CAN1 = PA11/PA12 (строки 281, 291) | **НЕТ**: дефолт CAN1 - PA11/PA12, не PB8/PB9 |
| LED/boot | (по netlist, нет данных) | - | `LED_BUILTIN` = PNUM_NOT_DEFINED (строка 121) | н/д - LED не входит в контракт |

Вывод: **расхождение подтверждено** на уровне дефолтов для 3 из 5 каналов (display UART, I2C, CAN) + именование E22-канала (USART6 vs Serial3). Все нужные capability есть в PeripheralPins - явная инициализация периферии на netlist-пинах работает (доказано bringup-can диaгнoстиком: явный AF9 CAN1 на PB8/PB9). Это и есть validation obligation #51 §14: stock board def НЕ воспроизводит V1-поведение -> Semantic-решение D5 (§6.1), НЕ скрытый workaround.

## 5. Диагностический CAN loopback (D1a)

- Файл: `bringup/can_loopback.cpp`, отдельный pio env `env:bringup-can` (extends common_firmware, build_src_filter `+<bringup/can_loopback.cpp>`).
- Логика: HAL_CAN init (500000, LoopBack mode), magic в RAM, отправка N=8 кадров, приём с проверкой данных, счётчики tx_ok/rx_ok/crc_err + magic в фиксированную RAM-область (0x20010000, `--section-start .bram_diag`), после прогона - `while(1)` (останов для чтения).
- Host: `bench/bringup/can_loopback_check.py` - `flash_diag(env="bringup-can")` (build + objcopy-strip маркер-секции + OpenOCD `reset halt` program) -> OpenOCD init/halt -> mdw RAM-области -> интерпретация -> exit 0/3 (pass/fail). Переиспользует tools из verification-runner (kill_openocd, REPO_ROOT, pio_cmd).
- Граница: внутренний loopback доказывает работу CAN-периферии; пины PB8/PB9 сконфигурированы (AF9) по netlist, электрическая целостность пинов/шины - #62 (CAN оснастка), в #61 не входит.
- Тест: host-тест на логику интерпретации результата (fake RAM-содержимое) - небольшой unittest в `bench/bringup/`.
- Политика: диагностика bring-up, НЕ production CAN HAL (bounded TX/RX, force-stop mailbox - Фаза 1, тикет #71/HAL-работа).

## 6. Watchdog arm (D2b - пересмотрено дважды)

**Факт**: на STM32F405 IWDG-регистры - KR (0x40003000), PR (0x40003004), RLR (0x40003008), SR (0x4000300C) (RM0090 §22.3). Читаемого down-counter'а НЕТ, бита «запущен» (WDGA) НЕТ. Неинвазивное регистровое доказательство арма невозможно.

**D2b (watchdog fire experiment, подтверждён владельцем)** - RAM phase-marker flow против false-pass:
- Опасность stale-флага: IWDGRSTF переживает NRST-загрузку (ST-Link upload не очищает RCC_CSR; очистка только записью 1 в RMVF). Без очистки повторный прогон прочитал бы бит 29 от ПРЕДЫДУЩЕГО эксперимента - false pass.
- `bringup/watchdog_fire.cpp`, env `bringup-watchdog` (RAM-маркер `.bram_wdg` на 0x2000F000, переживает и NRST, и IWDG-reset):
  1. boot, marker != ARMED: запись 1 в RCC_CSR.RMVF (очистка ВСЕХ reset-флагов, включая stale IWDGRSTF) -> marker = ARMED -> `IWatchdog.begin(2 s)` -> reload НЕ вызывается -> IWDG стреляет ~2 s (LSI 17-47 kHz -> до ~3.8 s).
  2. IWDG-reset (RAM сохранён) -> boot, marker == ARMED: это watchdog-перезагрузка -> marker = PASSED -> keep-alive reload каждые 100 ms (окно 2 s), board стабильна для чтения host'ом.
- Защита от stale-ARMED false-pass: RAM переживает и programming, и reset, поэтому interrupted-ран мог бы оставить `[magic, ARMED]`; следующий boot ушёл бы в PASSED-ветку без очистки RCC-флагов и без арма. `flash_diag(env="bringup-watchdog", zero_addrs=[0x2000F000, 0x2000F004])` программирует `verify` БЕЗ reset, обнуляет оба слова маркера через OpenOCD `mww` (halted), затем `reset run` - каждый прогон стартует с гарантированно чистого состояния.
- Host `bench/bringup/watchdog_fire_check.py`: flash env -> wait 6 s -> OpenOCD читает marker (0x2000F000, 2 слова) + RCC_CSR (0x40023874) -> PASS только если marker.magic == kMagic И marker.phase == PASSED И bit 29 (IWDGRSTF) = 1. FAIL иначе.
- Доказывает полный путь: arm + fire + reset + аппаратный флаг, без ложных срабатываний от stale-флага.
- Board сбрасывается один раз (безвредно: диагностическая прошивка, данных нет; production kernel после эксперимента возвращается флешем).

**D2-static (отклонено владельцем)**: чтение IWDG_PR/RLR + IWDGRSTF=0 после чистой загрузки + host-тесты kernel - доказывает конфигурацию и код-путь, НЕ фактический запуск IWDG и срабатывание.

## 6.1. Semantic-решение D5 (pin contract, принято владельцем)

**D5a (ПРИНЯТО владельцем 2026-08-13)**: принять stock board def как есть; pin contract фиксируется ЯВНОЙ инициализацией периферии в Phase-1 адаптерах на netlist-пинах (TwoWire на PB10/PB11, Serial1=USART1 PA9/PA10, USART6 на PC6/PC7 для E22, CAN1 AF9 на PB8/PB9). V3-прошивка пишется с нуля, адаптеры уже владеют пинами (образец - bringup-can diag). Отклонение дефолтов документируется в bring-up report. Board JSON override не нужен, custom variant fork ядра не вводится.

**D5b**: board JSON override (custom variant/board def с дефолтами netlist: Wire=I2C2 PB10/PB11, Serial=USART1 PA9/PA10, CAN=PB8/PB9) с impact-анализом по #51 §4 (board baseline; строка 59 - правило override). Воспроизводит V1-поведение на уровне дефолтов, но форкает ядро STM32duino (обслуживание, обновления, расхождение с upstream) и не нужен для V3 (адаптеры всё равно инициализируют периферию явно).

**D5c**: rebaseline netlist (изменить пины контракта под дефолты def) - НЕ применимо: пины определены PCB-разводкой, не выбором ПО.

## 7. Evidence record (D3)

- `docs/bring-up-report-v3.md` (tracked, single-context): pass/fail таблица E1-E7, pin-контракт (E5), watchdogs регистры, CAN счётчики, ссылки на runner evidence bundle (`out/`, untracked) и bench record #73.
- Формат: evidence record по #52 §7.1 (method/environment/evidence type), рабочий язык русский, идентификаторы wire-контракта нормативные.
- Firmware SHA/toolchain/bench identity - из runner result JSON (gitSha, toolchain {platformio, platform, core}, board {part, idcode, uid}).

## 8. Split владелец/агент

| Шаг | Исполнитель |
| --- | --- |
| Физическое подключение (ST-Link, UART, датчики) + energizing gate | владелец (процедура #73) |
| Checklist-подпись | владелец |
| Прогоны runner (E1/E2), CAN loopback (E4), watchdog read (E3) | агент (runner + OpenOCD) |
| Pin-сверка, report, тесты, PR | агент |

## 9. Риски и границы

- CAN трансивер на плате не подтверждён (netlist: CAN PB8/PB9; трансивер не указан) - внутренний loopback не зависит от трансивера; физическая шина - #62.
- UART мониторинг: MCU молчит (frozen flags) - ожидаемо, документируется; трафик появится с #72.
- Стенд-сессия требует владельца (checklist, физика) - планируется после мержа этого PR или в ходе; evidence из runner остаётся в `out/`.
- Изменение frozen baseline (если pin-расхождение или board override) - Semantic-класс, отдельное решение, НЕ скрытый workaround.

## 10. Состав PR #61 (после HITL)

- `docs/bringup-design-v3.md` (этот документ, решения §0 заполнены).
- `docs/bring-up-report-v3.md` (evidence record E1-E7).
- `bringup/can_loopback.cpp` + env `bringup-can` (D1a) + `bench/bringup/can_loopback_check.py`.
- `bringup/watchdog_fire.cpp` + env `bringup-watchdog` (D2b) + `bench/bringup/watchdog_fire_check.py`.
- `bench/bringup/flash_diag.py` (build + objcopy-strip loadable RAM-маркеров + OpenOCD reset-halt program: сырой flash-образ содержит RAM-сегмент, verify невозможен).
- `bench/bringup/bringup_logic.py` (чистая логика интерпретации) + `bench/bringup/tests/` (host-тесты, без железа).
- Обновление bench record #73 не требуется: report ссылается на #73 как канонический hardware-контракт.
