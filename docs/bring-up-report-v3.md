# Bring-up evidence record: ControllerV6 Phase 0 (ticket #61)

- Тикет: [#61 «Подтвердить bring-up и аппаратный контракт ControllerV6»](https://github.com/Driadix/ShuttleControllerV3/issues/61) (execution map #58)
- Дата/owner: 2026-08-13, Driadix (checklist-подпись в каждом runner-result)
- Дизайн: `docs/bringup-design-v3.md` (решения D1a/D2b/D3/D4/D5a)
- Метод: прогон сценариев verification-runner (#65) + bring-up диагностики (CAN loopback, IWDG fire experiment) вокруг физического стенда
- Environment: bench record `docs/l4-sensor-bench-v3.md` (#73), frozen toolchain #51 §3-4

## Bench identity (E1, live probe)

| Поле | Значение |
| --- | --- |
| part | STM32F405RG |
| idcode | 0x100f6413 |
| uid | 002900363033470336363131 |
| firmware gitSha | be5a0085490c78e0f664af85d6fbd2c2c2b7c86b (main @ PR #91, рабочее дерево dirty) |
| artifactSha256 | 959e234ea70cef3950d6723a6225928a42128852537d081d7dc0fd76acd46e88 |
| toolchain | platformio 6.1.19, ststm32@17.4.0, framework-arduinoststm32@4.20701.0 |

## Критерии перехода 0 -> 1 (`docs/implementation-plan-v3.md` §5, Фаза 0): pass/fail

| # | Критерий | Вердикт | Evidence |
| --- | --- | --- | --- |
| E1 | Boot под frozen toolchain | **PASS** | runner `flash-boot-smoke`: verdict PASS, evidence complete; `out/bringup-61/result-flash-boot-smoke.json` |
| E2 | Исправный мониторинг | **TIMEOUT (ожидаемое поведение)** | runner `uart-probe`: COM9 open, capture 20 s, 0 кадров, TIMEOUT «frames 0 < minFrames 1»; host-пайплайн (порт, capture, normalize, oracle) РАБОТАЕТ; MCU молчит - UART выключен frozen build flags (`NO_HW_SERIAL`, `HAL_UART_MODULE_DISABLED`, #51 §4), трафик появится с #72 (#60 §0.4); `out/bringup-61/result-uart-probe.json` |
| E3 | Watchdog arm | **PASS** | IWDG fire experiment: RAM-маркер `[0x5A5A5A5A, 0x5EED]` (PASSED) + RCC_CSR IWDGRSTF bit 29 = 1; полный путь arm + fire + reset подтверждён аппаратным флагом |
| E4 | CAN ping/loopback | **PASS** | CAN1 LoopBack 500 k: tx_ok 8, rx_ok 8, crc_err 0, done 1; периферия CAN1 подтверждена; пины PB8/PB9 (AF9) сконфигурированы по netlist (внутренний loopback не выходит на пины; эл. целостность - #62) |
| E5 | Пины сверены | **PASS (с расхождением дефолтов)** | таблица §3: capability есть, дефолты board def НЕ совпадают с netlist; Semantic-решение D5a принято владельцем |
| E6 | Sensor-bus presence | **PASS (reuse #73)** | bench record #73: 0x09/0x0C (ToF) + 0x36 (AS5600) отвечают, VALID clear, 2026-08-13 |
| E7 | Startup-to-Ready | **N/A** | не измеряется: нет полной прошивки (план §2 Фаза 0) |

## Pin-контракт (E5): сверка board def с netlist

Источники: `framework-arduinoststm32/variants/STM32F4xx/F405RGT_F415RGT/variant_generic.h` и `PeripheralPins.c` (установленное ядро), V1-индекс `docs/research/v1-system-evidence-index.md:47,49,139`.

| Периферия (netlist #73) | Пины | Capability | Default board def | Совпадение |
| --- | --- | --- | --- | --- |
| UART display | PA10 RX / PA9 TX (USART1) | USART1 PA9/PA10 AF7 | `Serial` = UART4 PA0/PA1 | НЕТ (нужен явный Serial1) |
| UART BMS | PA3 RX / PA2 TX (USART2) | USART2 PA2/PA3 AF7 | Serial2 = USART2 PA2/PA3 | да |
| UART LoRa E22 | PC7 RX / PC6 TX | USART6 PC6/PC7 AF8 | Serial3 = USART3 (PB10/PC10 TX) | НЕТ: PC6/PC7 на F405 = USART6; V1-индекс:49 называет канал Serial3 - именование V1 не соответствует hardware |
| I2C ToF + AS5600 | PB11 SDA / PB10 SCL (I2C2) | I2C2 PB11/PB10 AF4 | `Wire` = I2C1 PB7/PB6 | НЕТ (нужен явный TwoWire на PB10/PB11) |
| CAN | PB8/PB9 (CAN1) | CAN1 PB8/PB9 AF9 | первое вхождение CAN1 = PA11/PA12 | НЕТ (нужен явный AF9 PB8/PB9) |

Вывод: stock board def `genericSTM32F405RG` НЕ воспроизводит V1-поведение на уровне дефолтов (validation obligation #51 §14 доказана: 3 из 5 каналов + именование E22). Все нужные capability есть в PeripheralPins - явная инициализация на netlist-пинах работает (доказано E4: bringup-can явно конфигурирует AF9 CAN1 на PB8/PB9).

**Semantic-решение D5a (принято владельцем 2026-08-13)**: stock board def остаётся frozen; pin contract фиксируется ЯВНОЙ инициализацией периферии в Phase-1 адаптерах (TwoWire PB10/PB11, Serial1 USART1 PA9/PA10, USART6 PC6/PC7 для E22, CAN1 AF9 PB8/PB9). Board JSON override и fork ядра не вводятся. Отклонение дефолтов остаётся задокументированным.

## Watchdog (E3): детали

- Метод: IWDG fire experiment (design §6, D2b). Единственное доказательство arm+firing на F405: у IWDG нет читаемого счётчика и бита WDGA (RM0090 §22.3).
- RAM phase marker (0x2000F000, переживает NRST и IWDG reset): boot 1 - очистка RCC_CSR.RMVF (защита от stale IWDGRSTF), marker=ARMED, arm 2 s без reload; после fire - boot 2: marker=PASSED, keep-alive reload.
- Результат: marker `[0x5A5A5A5A, 0x5EED]`, RCC_CSR = 0x24000000 (IWDGRSTF bit 29 = 1 + PINRSTF bit 26 = 1; decode по заголовку ядра stm32f405xx.h: RMVF=bit 24, PINRSTF=bit 26, IWDGRSTF=bit 29, WWDGRSTF=bit 30). Boot-1 очищал ВСЕ reset-флаги (RMVF write-1) до арма, значит оба бита установлены самим reset-событием IWDG; дискриминатор - bit 29 (IWDGRSTF), подтверждён маркером PASSED.
- Полный путь arm + fire + reset подтверждён; плата сбросилась один раз (диагностика, данных нет).

## CAN (E4): детали

- Метод: bringup-can env, CAN1 LoopBack 500 kbit/s (prescaler 4, 21 tq/bit), 8 кадров StdId 0x123, паттерн данных, последовательная TX/RX-проверка (RX FIFO глубиной 3).
- Результат: magic 0xCA11D1A6, tx_ok 8, rx_ok 8, crc_err 0, done 1.
- Доказывает: CAN-периферию, 500 kbit/s конфигурацию (внутренний loopback не выходит на пины). Пины PB8/PB9 (AF9) сконфигурированы по netlist. НЕ доказывает: электрическую целостность пинов, трансивер, физическую шину, CAN-пир (оснастка #62, отдельный тикет).

## Процедура и состояние стенда

- Последовательность: E1 flash-boot-smoke -> E2 uart-probe -> E3 watchdog fire -> E4 CAN loopback -> возврат production firmware (flash_diag env=firmware, reset-halt).
- Флеш bring-up диагностик выполняется через OpenOCD `reset halt` + flash-образ с вырезанными RAM-маркерами (objcopy --remove-section): диагностические ELF содержат loadable RAM-сегмент (маркер-секции) - сырой objcopy-образ раздувается до ~400 МБ и OpenOCD не может верифицировать RAM-адрес во flash (Verify Failed); вырезание маркеров даёт чистый flash-образ. `reset halt` + обнуление маркеров дают детерминированное стартовое состояние (следующий bullet).
- Watchdog-прогон дополнительно обнуляет маркерные слова через OpenOCD `mww` перед `reset run` (RAM переживает programming/reset; stale-ARMED маркер от прерванного прогона иначе мог бы дать false pass).
- Стенд после сессии: production kernel (firmware env) прошит, ST-Link/COM9 на месте.
- Evidence bundle: `out/bringup-61/` (gitignored): result-flash-boot-smoke.json, result-uart-probe.json, raw-*.bin.

## Заключение по переходу 0 -> 1

| Критерий (план §5 Фаза 0) | Вердикт | Статус перехода |
| --- | --- | --- |
| Плата boot-ится под frozen toolchain | PASS | закрыт |
| Пины сверены | PASS (расхождение дефолтов, D5a принято) | закрыт |
| Исправный мониторинг | TIMEOUT (host-пайплайн работает, MCU-трафик ожидаемо отсутствует) | pipeline закрыт; трафик deferred до #72 (#60 §0.4) |
| CAN-пинг | PASS (loopback; шина/трансивер - #62) | закрыт на уровне периферии; физическая шина deferred до #62 |
| Watchdog-арм | PASS (fire experiment, IWDGRSTF) | закрыт |
| Startup-to-Ready | N/A (нет полной прошивки, план §2) | не применимо по плану |

Вывод: bring-up под production baseline подтверждён для всех исполнимо-проверяемых критериев; задокументированные deferrals (UART-трафик #72, CAN-шина #62, I2C live #63) соответствуют плану и #60 §0.4 и не являются скрытыми фейлами.

## Риски и границы

- CAN трансивер/физическая шина не проверены (нет CAN-оснастки) - #62 (CAN/timing), критерий loopback закрыт.
- UART-трафик MCU отсутствует до #72 (frozen flags отключают UART; мониторинг-путь COM9 работает).
- I2C live re-verify невозможен без I2C-драйвера (Фаза 1, #63); presence зафиксирован #73 (2026-08-13).
- gitDescribe `be5a008-dirty`: рабочее дерево содержит незакоммиченный bring-up слайс (этот PR).
