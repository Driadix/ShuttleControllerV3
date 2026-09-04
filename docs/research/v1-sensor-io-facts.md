# Факты по дискретным входам/датчикам V6+V1 (для стенда V3)

Evidence-файл карты стенда миграции. Источники: netlist `docs/PCB/ControllerV6/ControllerV6.kicad_sch` (сгенерирован kicad-cli -> `.tmp/v6.net`), V1-исходник `Driadix/ShuttleController@708d090` `Cntrl_V2/Cntrl_V2.ino` (клонирован локально). Дата: 2026-09-01. Полный pin-map MCU + разъёмов - приложение A.

## Сводная таблица

| Разъём | Сеть (сторона датчика) | Цепь после изолятора | MCU-пин | V1 pinMode / логика |
| --- | --- | --- | --- | --- |
| XT1 Bumper_F1 | BumpF1_In | D5.10 -> BumpF1_In_ | U2.pin59 (PB7) | INPUT, FALLING-IRQ, active-HIGH (срабатывает edge) |
| XT2 Bumper_F2 | BumpF2_In | D5.14 -> BumpF2_In_ | U2.pin55 (PB3) | INPUT, FALLING-IRQ |
| XT3 Bumper_R1 | BumpR1_In | D6.12 -> BumpR1_In_ | U2.pin53 (PC12) | INPUT, FALLING-IRQ |
| XT4 Bumper_R2 | BumpR2_In | D6.15 -> BumpR2_In_ | U2.pin50 (PA15) | INPUT, FALLING-IRQ |
| XT11 Pallet1 | Net-(XT11-Pin_1) | R13.2 -> Pallet1 net | U2.pin58 | INPUT_PULLUP, active-LOW |
| XT12 Pallet2 | Net-(XT12-Pin_1) | R14.2 -> Pallet2 net | U2.pin54 | INPUT_PULLUP, active-LOW |
| XT13 Pallet3 | Net-(XT13-Pin_1) | R15.2 -> Pallet3 net | U2.pin21 | INPUT_PULLUP, active-LOW |
| XT14 Pallet4 | Net-(XT14-Pin_1) | R16.2 -> Pallet4 net | U2.pin51 | INPUT_PULLUP, active-LOW |
| XT15 Lifter UP | Lifter UP | D6.10 -> Lifter UP_ | U2.pin2 (PA0) | INPUT_PULLUP, active-LOW |
| XT18 Lifter DOWN | Lifter DOWN | D5.13 -> Lifter DOWN_ | U2.pin56 | INPUT_PULLUP, active-LOW |
| XT9 Channel_in | Net-(XT9-Pin_1) | R11.2 -> In channel | U2.pin57 (PB5) | INPUT (приёмка активного состояния) |
| XT10 Reserve | Net-(XT10-Pin_1) | R12.2 -> Reserve | U2.pin52 | ADC-capable? см. приложение |

## Ключевые выводы для стенда

1. **Полярности подтверждены** V1-источником (не только netlist):
   - Паллетные (4) и концевики лифтера (2): `INPUT_PULLUP`, **active-LOW** — «объект есть» = логический 0. `detect_Pallete` (`.ino:3406-3464`) с дебаунсом до 3x delay(5).
   - Бамперы (4): `INPUT` без pull, **FALLING-edge IRQ** (`attachInterrupt` `.ino:1691-1694`); ISR только флаги, foreground `handlePendingCrash` (`.ino:7798`) -> `FAULT_BUMPER_*` + `motor_Force_Stop()`.
   - Лифтер: `while(digitalRead(DL_UP))` крутится, пока НЕ нажат (`active-LOW`); таймаут `lifterDelay=3800` -> `FAULT_LIFTER_TIMEOUT`.
2. **Все датчики на изолированном домене** `5V/GNDREF`, сигнал проходит изоляторы `CA-IS3760` (D5) / `D6` на MCU-домен `3.3V_1/GND`. Следовательно, на стенде стимулятор должен подавать сигнал на СТОРОНУ ДАТЧИКА (5V/GNDREF, активный уровень), а не на MCU-пин.
3. **Входной каскад**: последовательные резисторы R11-R16 (200 Ом) между разъёмом и линией к изолятору; подтяжек к рейке на стороне датчика в netlist нет — высокий уровень задаёт `INPUT_PULLUP` MCU-стороны. Для активного-LOW датчика «срабатывание» = притягивание линии к GNDREF.
4. **RS-485 BMS**: U2.pin16=DI (TX), pin17=RO (RX), pin34=RE/DE (объединены на D7.4/5 + R36). A/B на XT23 через D7 (CA-IS3082) + R42 (120 Ом = терминация). DE управляется `digitalWrite(RS485)` → `RS485 = PB13` (!!) — но netlist не показывает PB13. **Расхождение V1 PB13 vs V6-PB11/резерв: уточнить на сайте**.
5. **ATEMP**: встроенный MCU-сенсор STM32, `analogRead(ATEMP)` (`.ino:1725,8856`), формула `25 + ((analogRead*3200)/4096 - 760)/2.5`. Внешнего термистора на PCB нет (Reserve XT10 — ADC-пин U2.52, но ATEMP внешний датчик по решению владельца не ставится). HZ-16 через внутренний сенсор + BMS NTC.

## Импликация типов датчиков (для сайт-чек-листа T5)

| Периферия | Режим | Типичный физический датчик | Проверка на сайте |
| --- | --- | --- | --- |
| Паллетные (4) | active-LOW, NN | индуктивный NPN-NO (открытый коллектор к GNDREF) или фото/отражательный PNP-NO | определить NPN/PNP, NO/NC, питание |
| Бамперы (4) | FALLING | механический переключатель NO к GNDREF (нажатие -> низкий фронт) | определить тип, область, полярность |
| Лифтер UP/DOWN (2) | active-LOW | механический концевик NO к GNDREF | определить NO/NC, монтаж |
| Channel_in (1) | приёмка | наличие шаттла в канале (индуктивный/фото) | определить тип |

**Все типы физических датчиков остаются UNKNOWN до сайт-чек-листа T5** — это единственный блокер для спроектированного стендового стимулятора.

## Приложение A — pin-map MCU (U2 STM32F405RGTx) по netlist

| MCU-пин | Сеть | Периферия |
| --- | --- | --- |
| 2 (PA0) | Lifter UP_ | концевик лифтера UP |
| 8 (PA1-tbd) | Lora_Gpio_ | E22 вспом. GPIO |
| 9 (PC1) | Green Led_ | зелёный LED |
| 10 (PC2) | White Led_ | белый LED |
| 11 (PC3) | Red Led_ | красный LED |
| 14 (PA5) | Buzzer_ | зуммер |
| 15 (PA3) | Led | LED пользователя |
| 16 (PA2) | DI RS485 | BMS TX |
| 17 (PA0-alt) | RO RS485 | BMS RX |
| 21 (PC3-alt) | Pallet3_ | паллетный датчик 3 |
| 29 (PB10) | SCL_ | I2C SCL (ToF+AS5600) |
| 30 (PB11) | SDA_ | I2C SDA |
| 34 (PB12-tbd) | RE RS485 | BMS DE |
| 35 (PB14) | Lora_M1_ | E22 M1 |
| 36 (PB15) | Lora_M0_ | E22 M0 |
| 37 (PC6) | Lora_TX_ | E22 TX |
| 38 (PC7) | Lora_RX_ | E22 RX |
| 42 (PA9) | Esp32 TX_ | мост TX |
| 43 (PA10) | Esp32 RX_ | мост RX |
| 50 (PA15) | BumpR2_In_ | бампер назад 2 |
| 51 (PB6) | Pallet4_ | паллетный датчик 4 |
| 52 (PB5) | Reserve_ | Reserve / ADC |
| 53 (PC12) | BumpR1_In_ | бампер назад 1 |
| 54 (PB4) | Pallet2_ | паллетный датчик 2 |
| 55 (PB3) | BumpF2_In_ | бампер вперёд 2 |
| 56 (PB3-alt) | Lifter DOWN_ | концевик лифтера DOWN |
| 57 (PB5-alt) | In channel_ | датчик канала |
| 58 (PB7) | Pallet1_ | паллетный датчик 1 |
| 59 (PB7-alt) | BumpF1_In_ | бампер вперёд 1 |
| 61 (PB8) | Can RX_ | CAN RX |
| 62 (PB9) | Can TX_ | CAN TX |

Примечание к приложению: имена пинов в скобках — проекция V1-дефайнов, netlist даёт только pad-номер. Сопоставление pad->GPIO по стандартной карте F405RGTx. Пины 20,22-27,33,39-41,44-45 (PA4,PA6,PA7,PA8,PB0,PB1,PB12,PC4,PC5,PC8,PC9,PA11,PA12) — `unconnected` в netlist.
