# Инвентаризация интерфейсов актуаторов и питания V1

Evidence-файл карты стенда миграции V3. Источники: V1-репо `Driadix/ShuttleController@708d090980155d4a8d4644f7bcf87c383e81cd1d` (`Cntrl_V2/...`), `docs/research/v1-system-evidence-index.md`, `v1-execution-evidence.md`, `v3-capability-evidence-slices.md` (gate PASS 2026-08-04), V3-доки: safety-authority/implementation-plan/bring-up/quality-attributes/safety-model. Дата: 2026-09-01.

## 1. Привод движения (CAN)

- CAN1: PB8 TX / PB9 RX, 500 kbit/s, extended ID (`.ino:353-386`; индекс:15; C01 `.ino:1705-1738`).
- Кадр extended ID 100, len 4, big-endian int32: `(minSpeed + spd*maxSpeed/100) * 1000`; spd 0..100 (кэп); дефолты maxSpeed=96, minSpeed=3; знак = `motorReverse ^ inverse`; rate-limit >= 50 мс; ramp ~18..80 мс/ступень (индекс:28; `.ino:2088-2154`, `2156-2221`).
- Force-stop V1: `motor_Force_Stop()` = одиночный zero-speed кадр ID 100, отдельного min-ID нет (`.ino:2087-2355`).
- U10 (unknown): стоп-паттерн `motor_Stop` с грузом - 10x zero-кадров x100 мс без SystemYield; менять только после HIL (bundle, Блокирующие unknowns U10).
- V3 force-stop (дизайн): extended ID `0x00000001`, payload `0xFF x8`, из Safety Authority (safety-authority-design-v3.md §4.3; implementation-plan-v3.md L116; proving-slice F1). **Поддержка приводами - открытый пункт F4: UNKNOWN.**
- Факт владельца (2026-09-01): приводы = FLIPSKY DUAL FSESC75100 (Alu PCB), моторы штатные. Протокол интерпретация: ID 100/101 = VESC CAN `SET_DUTY` (cmd 0) на controller_id 100 (движение) / 101 (лифтер); RX 2405 = `0x0965` = CAN_PACKET_STATUS (0x09) | controller_id 101, bytes[4..5] BE = ток; порог 500 = 50 A (x100 мА). Ожидает подтверждения research-тикетом карты стенда.

## 2. Лифтер (CAN)

- Кадр extended ID 101, len 4, BE int32; ramp ±5..±45 x1000 (9 ступ x 30 мс); полная ±50000; **вниз = положительное**; `lifter_Stop` пишет 0 (индекс:29; `.ino:2361-2450`, `2453-2535`).
- Концевики DL_UP PC13 / DL_DOWN PB4, INPUT_PULLUP, **активный LOW**: `while(digitalRead(DL_UP))` (крутится, пока НЕ нажат); таймаут `lifterDelay=3800 мс` -> `FAULT_LIFTER_TIMEOUT`.
- Ток лифтера: CAN ID 2405 (RX only), `buf[4]*256+buf[5]` raw; семпл только в `lifter_Up`, максимум после k>3; порог 500 (зажим 250). **Единицы до VESC-подтверждения - UNKNOWN (interpreted: 100 мА).**

## 3. Бампер

- 4x GPIO INPUT (без pull), falling-edge IRQ: BUMPER_F1 PB7, F2 PB3, R1 PC12, R2 PA15 (`.ino:353-386`; bring-up E5).
- ISR: только `crashSourceMask`+`crashPending`; foreground `handlePendingCrash` (`.ino:7735-7833`): латч `FAULT_BUMPER_*` + `motor_Force_Stop()` + счётчик.
- **Тип датчика - UNKNOWN** (сайт-чеклист карты стенда).

## 4. Датчики паллет

- 4x INPUT_PULLUP, пары: F1 PA5 / F2 PC4, R1 PB6 / R2 PD2 (`.ino:353-386`). **Активный LOW** (false = паллет есть); дебаунс до 3x delay(5) (`detect_Pallete` `.ino:3406-3464`).
- Роли: детект досок, boardCount/3, защита от двойного счёта (`.ino:6052-6134`); профиль 800 single-load требует обе пары, иначе `WARN_PALLET_SIZE_ERROR`.
- **Тип (индуктивный/фото) - UNKNOWN** (сайт-чеклист).

## 5. ToF

- Waveshare TOFSense (комментарий «Waveshare team», TOF_Sense.h); I2C2 PB11 SDA / PB10 SCL, 100 кГц. 4 модуля 0x09..0x0C (=0x08+ID): 0x09 ID1 ChR, 0x0A ID2 ChF, 0x0B ID3 PalR, 0x0C ID4 PalF.
- Чтение: I2C-блок 0x20..0x2C (13 байт: system_time, distance mm u32, dis_status u16, signal_strength u16, precision). Round-robin 1 чтение/вызов, >= 8 мс между датчиками; фильтр: потолок 1500 мм, окно 16, медиана 5, stale 300 мс, grace 1 с.
- **Вариант модуля (B/C/D) - UNKNOWN** (wiki B и D в docs/PCB без привязки).

## 6. AS5600

- I2C 0x36, рег угла 0x0E, 12-бит + magnitude + диаг. Назначение: счётчик оборотов колеса (`turnCount`, `weelDia=100`); `set_Position()` интегрирует дельты через `calibrateEncoder_F/R[8]` (дефолт {40..}) (`.ino:3922-3993`).
- Сервис 250 мс; stale 1 с; fault после 3 сбоев подряд. Восстановление калибровки из flash **закомментировано** (`.ino:7612-7615`).
- **Монтаж (ось колеса vs вал мотора) - UNKNOWN.**

## 7. ATEMP

- **Встроенный датчик STM32** через `analogRead(ATEMP)` (STM32duino ADC), 12-бит @3.2 В: `25 + ((analogRead*3200)/4096 - 760)/2.5`; публикация 0.1 °C (индекс:51; `.ino:1725`, `8851-8868`). **НЕ внешний термистор**; NTC (до 4) - только от BMS.
- Решение владельца (2026-09-01): внешний термодатчик пока не ставится; V3 резервирует интерфейс, HZ-16 через встроенный MCU-сенсор + BMS NTC.
- V3 пороги: warn 90 / fault 110 °C, гистерезис 5 °C.

## 8. BMS

- Протокол «DdA5» (BmsDdA5.hpp): TX `DD A5 cmd 00 cksumH cksumL 77` (7 б); RX `DD cmd status len payload... cksum 77` (64 б). Команды BasicInfo=0x03 / CellInfo=0x04 / DeviceInfo=0x05.
- RS-485: USART2 PA2 TX / PA3 RX, 9600, полудуплекс, **DE PB13**.
- Данные: packVoltage_mV=u16BE(p0)*10, ток, ёмкости cAh, cycleCount, protectionFlags, fetStatus, soc p19 (0..100), seriesCellCount, ntcCount, NTC `i16BE-2731` (<=4), <=24 ячейки мВ, device-info 24 б.
- Опрос: Basic 5 с idle / 15 актив / 60 high-load / 5 low-batt / 1 старт; Cells 60 с; Device 300 с; staleWarnMs=300 с. `minBattCharge=20`; fatal: лифтер вниз + вперёд + `FAULT_LOW_BATTERY` (`.ino:7700-7737`).
- **Модель BMS-железа - UNKNOWN.**

## 9. Батарейный пакет

- **Номинальное напряжение нигде не заявлено**; **химия/число ячеек статически UNKNOWN** (seriesCellCount из BMS в рантайме).

## 10. Профили 800/1000/1200

`shuttleLength` (дефолт 1000, без валидации) - только геометрия/тюнинг, НЕ мощность привода: stop offset -25 для 1000/1200; pickup/board 600 база, 1200->670; unload 500 для 800; recapture 100/250/450; board-delay maxbb -3 для 1200; 800 single-load - обе пары сенсоров, иначе `WARN_PALLET_SIZE_ERROR`; `pltMaxLn=shuttleLength-20`.
Факт владельца (2026-09-01): полигонный шаттл - профиль 800; электроника и железо от профиля не зависят (только радиус колеса и длина шаттла - tuning, не смена hardware).

## 11. Радио E22

- Ebyte E22-series; USART6 PC6 TX / PC7 RX 57600; M0 PB15 / M1 PB14; AUX PC0 (input pullup).
- Профиль: addrH 0x01, узел 1..32, канал 30 -> 440125 кГц (410125+ch*1000), air 4800 бит/с, TX 27 дБм, fixed, 240-байт субпакеты, RSSI append, netID 0, crypt 0, LBT off, 8N1.
- Транзакции: <=2 попытки, 100 мс mode settle, AUX 300 мс, guard 20 мс; бэкофф 5/30/120/600 с, probe 15 с тишины, аудит 300 с.
- **Вариант модуля - UNKNOWN.**

## 12. Физический шаттл

- `weelDia=100`; `shuttleLength` 800/1000/1200 мм. Скорость 1.0..1.7 м/с - оценка V3-бюджета, не замер. **Масса, геометрия каналов - UNKNOWN.**
- Дисплей USART1 PA10 RX / PA9 TX 230400 8E1 (мост); LED: GREEN PC1 / WHITE PC2 / RED PC3, BOARD PA1, ZOOMER PA0.

## Сводка UNKNOWN (киев сайт-чекиста / полигона)

1. Модель/протокол привода: владелец заявил FSESC75100 Dual (VESC) - ожидает подтверждения research-тикетом; units кадра 100 (U01).
2. Units тока 2405 и порога 500 - интерпретация 100 мА/50 А до подтверждения.
3. Тип датчиков паллет / бамперов (индуктивные/фото/NPN/PNP/NO/NC) - сайт-чеклист.
4. Вариант ToF (B/C/D) - сайт-чеклист.
5. Монтаж AS5600 (ось колеса vs вал мотора) - сайт-чеклист.
6. Модель BMS и напряжение/химия пакета - сайт-чеклист.
7. Вариант E22 - сайт-чеклист.
8. Масса шаттла/каналы - полигон.
9. Семантика стоп-паттерна 10x100 мс (U10, HIL).
10. Поддержка приводами force-stop ID 0x1 (F4, открыт в #58).