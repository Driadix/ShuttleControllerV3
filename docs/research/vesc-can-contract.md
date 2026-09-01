# VESC CAN-контракт привода FLIPSKY DUAL FSESC75100 (V1 → V3)

Research-тикет #104 карты #103. Вопрос: какой в точности CAN-протокол реализует привод и как V1-кадры (ID 100/101, RX 2405) ложатся на него. Закрывает UNKNOWN U01/U02 из `v1-actuation-inventory.md` (пп. 1-2, 9-10) и даёт вход для F4 (#58, force-stop min-ID 0x1) и протокольного эмулятора стенда.

## Evidence metadata

- **Source**: первичные исходники VESC: `vedderb/bldc` master, SHA `f2cd239b301398fa09574b4df9d5b7b245c84183` (2026-08-31, клон в `.tmp/bldc`); `vedderb/vesc_tool` master, SHA `2721f9828eb2d05560d0774989e6156b6ca5b1ac` (2026-08-28); официальная дока `bldc/documentation/comm_can.md` (в дереве того же SHA). Vendor: страницы Flipsky DUAL FSESC75100 (Alu PCB), поиск 2026-09-01: [technobotix](https://www.technobotix.in/products/flipsky-dual-75100-with-aluminum-pcb-based-on-vesc/1781252000001206373), [aliexpress](https://www.aliexpress.com/item/1005004267433267.html). V1-сторона: `docs/research/v1-actuation-inventory.md` пп. 1-2.
- **Version pinning**: все исходник-якоря вида `bldc@f2cd239 <path>:L<start>-L<end>`; permalink-эквивалент `https://github.com/vedderb/bldc/blob/f2cd239b.../<path>#L..`. Контракт пакетов 0-9/14-27 (`CAN_PACKET_*`, EID-кодировка, масштабы payload) стабилен между VESC-firmware 5.x и 6.x: значения enum в доке `comm_can.md` (входит в f2cd239) совпадают с enum в коде того же SHA. Утверждения о том, что именно залито в конкретные приводы (форк Flipsky, vendor рекомендует заводскую 5.2), помечены UNKNOWN до bench-прочтения конфигурации.
- **Confidence**: протокольные факты (кодировка, масштабы, маршрутизация, timeout-механика) - source-verified, высокая. Конфигурация конкретных приводов V1 (ids, timeout, status rate, single/dual топология, firmware) - UNKNOWN, список в конце. Файл не норматив: disposition по каждому пункту решает verification gate владельца.

## 1. Протокол: VESC CAN-mode (firmware bldc)

- Привод FLIPSKY DUAL FSESC75100 - VESC-based контроллер (vendor: «based on VESC», «programmable via VESC_TOOL», CAN-порт; [technobotix], [aliexpress]). Его CAN-язык - фирменный VESC CAN-mode firmware bldc (не UAVCAN/DroneCAN, не Comm Bridge; дефолт и рекомендованный режим `CAN_MODE_VESC`: `bldc@f2cd239 documentation/comm_can.md:3-12`; дефолт `APPCONF_CAN_MODE CAN_MODE_VESC`: `applications/appconf_default.h:33` area, применяется `confgenerator.c:901+`).
- Скорость: дефолт 500 kbit/s (`APPCONF_CAN_BAUD_RATE CAN_BAUD_500K`, `bldc@f2cd239 applications/appconf_default.h:55`; маппинг 500→`CAN_BAUD_500K` `comm/comm_can.c:207-224`). Совпадает с шиной V1 (500 kbit/s, inventory п.1).
- Кадры: только 29-bit extended ID (`bldc@f2cd239 documentation/comm_can.md:24-32`). Кодировка EID: B28-B16 не используются, B15-B8 = command ID (packet type), B7-B0 = VESC ID (controller_id), т.е. **EID = (packet_type << 8) | controller_id** (`documentation/comm_can.md:28-32`; в коде: `comm_can_set_duty` строит `controller_id | (CAN_PACKET_SET_DUTY << 8)`, `comm/comm_can.c:511-512`; разбор `id = eid & 0xFF; cmd = eid >> 8`, `comm/comm_can.c:1586-1587`).
- enum `CAN_PACKET_ID` (ключевые): `bldc@f2cd239 datatypes.h:1154-1225`:

| Packet | Значение | Payload (simple: 4 байта BE int32 c масштабом) |
| --- | --- | --- |
| CAN_PACKET_SET_DUTY | 0 | duty × 100000 (диапазон -1.0..1.0) |
| CAN_PACKET_SET_CURRENT | 1 | ток мотора × 1000 (А) |
| CAN_PACKET_SET_CURRENT_BRAKE | 2 | ток торможения × 1000 |
| CAN_PACKET_SET_RPM | 3 | ERPM × 1 |
| CAN_PACKET_SET_POS | 4 | градусы × 1000000 |
| CAN_PACKET_PROCESS_SHORT_BUFFER | 8 | обёртка COMM_*-команды (см. п. 4) |
| CAN_PACKET_STATUS | 9 | статус 1: rpm/current/duty (таблица ниже) |
| CAN_PACKET_STATUS_2..6 | 14,15,16,27,58 | Ah/Wh, температуры, тахометр/напряжение, ADC |
| CAN_PACKET_PING / PONG | 17 / 18 | ping: 1 байт id отправителя; pong: [id, HW_TYPE] |
| CAN_PACKET_SHUTDOWN | 31 | 8 нулевых байт, питание-даун (если HW поддерживает) |

- Simple-команды: 4 байта данных, BE int32 со масштабом (`documentation/comm_can.md:34-56`). BE-кодировка подтверждена исходником: `buffer_get_int32` собирает B0<<24|B1<<16|B2<<8|B3 (`bldc@f2cd239 util/buffer.c:169-176`), `buffer_get_float32 = int32/scale` (`util/buffer.c:218-220`).
- **SET_DUTY**: payload = `(int32_t)(duty * 100000.0)` BE (`bldc@f2cd239 comm/comm_can.c:507-513`); приём: `mc_interface_set_duty(buffer_get_float32(data8, 1e5, &ind))` + `timeout_reset()` (`comm/comm_can.c:1607-1611`).
- **STATUS (packet 9)**, 8 байт BE (`bldc@f2cd239 comm/comm_can.c:1216-1224`; таблица `documentation/comm_can.md:214-220`):
  - B0-B3: ERPM, int32, масштаб 1;
  - B4-B5: ток мотора, int16, масштаб 10 (0.1 А/LSB), значение = `mc_interface_get_tot_current_filtered()` - **моторный (q-axis, создающий момент) фильтрованный ток со знаком**, не батарейный (батарейный - `STATUS_4` B4-B5, `comm_can.c:1244-1253`; семантика q-тока: `motor/mcpwm_foc.c:1179-1202`);
  - B6-B7: duty, int16, масштаб 1000.
- Статусные кадры **отправляются узлом** под EID `(9 << 8) | controller_id` (и, для dual, `(9 << 8) | (controller_id+1)`, см. п. 5); **принимаются от любого id** и складываются в таблицы `stat_msgs` («addressed to all devices»: `bldc@f2cd239 comm/comm_can.c:2032-2048`).
- Включение статусного вещания: appconf `can_status_msgs_r1/r2` (битовая маска) + `can_status_rate_1/2` (Гц). Дефолты: **msgs_r1 = 0, msgs_r2 = 0 (вещание выключено)**, rates 50 и 5 Гц (`bldc@f2cd239 applications/appconf_default.h:66-76`, применяются `confgenerator.c:905-907`). Отправка: потоки `cancom_status_thread_1/2` (`comm/comm_can.c:1527-1570`).
- Прочее: мультикадровые обёртки (`FILL_RX_BUFFER` 5 / `FILL_RX_BUFFER_LONG` 6 / `PROCESS_RX_BUFFER` 7) для COMM-команд длиннее 6 байт с CRC16 (`bldc@f2cd239 comm/comm_can.c:443-505, 1695-1767`); `PROCESS_SHORT_BUFFER` 8 - до 6 байт без CRC (`comm/comm_can.c:1769-1808`). VESC Tool подтверждает масштабы со своей стороны: `setDutyCycle` = `COMM_SET_DUTY` + double32 × 1e5 (`vesc_tool@2721f98 commands.cpp:1328-1334`), `forwardCanFrame` = `COMM_CAN_FWD_FRAME` (`commands.cpp:1979-1987`).

## 2. Разбор кадров V1 в терминах VESC

- **ID 100 (движение) = EID 0x64 → cmd 0 (CAN_PACKET_SET_DUTY), controller_id 100. ПОДТВЕРЖДЕНО.** Payload 4 байта BE int32, duty = value/100000. V1-формула `(minSpeed + spd*maxSpeed/100)*1000` при дефолтах даёт 3000..99000 → **duty 0.03..0.99**; знак = направление вращения. Закрывает **U01 (units кадра 100)**: units = duty × 1e5, НЕ проценты/rpm.
- **ID 101 (лифтер) = EID 0x65 → SET_DUTY, controller_id 101. ПОДТВЕРЖДЕНО.** Ramp ±5..±45×1000 и полная ±50000 → **duty ±0.05..±0.45 / ±0.5**; «вниз = положительное» - вопрос конфигурации направления конкретного привода (`m_invert_direction` в mcconf меняет знак: `mc_interface.c:52` DIR_MULT, `motor/mc_interface.c:558-582`), протокольно это просто знак duty.
- **RX 2405 = EID 0x0965 → cmd 9 (CAN_PACKET_STATUS), controller_id 101 (лифтер). ПОДТВЕРЖДЕНО.** V1 читает `buf[4]*256+buf[5]` = B4-B5 BE int16 STATUS-тока, масштаб 10 → 0.1 А/LSB = «×100 мА» в интерпретации V1. **Порог 500 = 50.0 А - подтверждён** (500 × 0.1 А). Закрывает **U02**. Нюансы для эмулятора/телеметрии: поле = моторный (моментный) ток со знаком, не входной батарейный; диапазон int16 ±3276.7 А; B0-B3 этого же кадра = ERPM лифтера (масштаб 1), B6-B7 = duty (×0.001) - V1 их не читает, но эмулятору допустимо заполнять осмысленно.
- Итого интерпретация владельца из inventory п.1 (движение=100, лифтер=101, ток=bytes[4..5] BE ×100 мА, порог 500=50 А) **подтверждается полностью** на протокольном уровне. Конкретная конфигурация приводов (что ids реально 100/101, что статусное вещание включено у лифтера) остаётся bench-проверкой (см. UNKNOWN).

## 3. Реакция на duty=0 и потерю команд

- **Приём любой SET_*-команды всегда делает `timeout_reset()`** (`bldc@f2cd239 comm/comm_can.c:1610` и все case SET_*; то же для COMM-пути `comm/commands.c:485-518`). Cadence V1 ≥ 50 мс между кадрами кормит таймаут с запасом.
- **Timeout живёт в appconf (не mcconf)**: `timeout_msec`, `timeout_brake_current`, `kill_sw_mode` (`bldc@f2cd239 datatypes.h:913` area; применяются `main.c:415-416` и `comm/commands.c:638`; в VESC Tool - App Settings → General). Дефолты кода: **timeout_msec = 1000 мс, timeout_brake_current = 0.0** (`applications/appconf_default.h:27-31`, `timeout.c:43`). Расхождение с докой: `documentation/comm_can.md:16` пишет «0.5 seconds» - код авторитетен (1000 мс); фактическое значение на приводах V1 - UNKNOWN (bench read appconf).
- **Механика таймаута** (`bldc@f2cd239 timeout.c:187-263`, поток каждые 10 мс): при `timeout_msec != 0` и отсутствии `timeout_reset()` дольше timeout: `mc_interface_release_motor_override()`, unlock, затем **на оба мотора `mc_interface_set_brake_current(timeout_brake_current)`** (`timeout.c:244-252`). При дефолтном brake current 0: `mcpwm_foc_set_brake_current(0)` ставит CONTROL_MODE_CURRENT_BRAKE с iq=0 и выходит до перехода в RUNNING (`motor/mcpwm_foc.c:833-845`) - фазы отпускаются, мотор **coast** (свободный выбег, без активного торможения). Если задан ненулевой timeout_brake_current - активное торможение этим током. `timeout_msec = 0` отключает механизм полностью (не рекомендуется докой `documentation/comm_can.md:16`).
- **duty = 0 (SET_DUTY 0)**: `mcpwm_foc_set_duty` ставит CONTROL_MODE_DUTY с set=0 и MC_STATE_RUNNING (`bldc@f2cd239 motor/mcpwm_foc.c:721-729`); в runtime duty-контроля PI-регулятор загоняет модуляцию к нулю (`mcpwm_foc.c:3400-3448`) - активный спал к нулевому duty, мотор остаётся под управлением (не отпущен), дальнейшая команда любого знака мгновенно действует. Это и есть «быстрый стоп» семантики V1 `motor_Force_Stop` (одиночный zero-кадр ID 100).
- **Что считать сейф-стопом привода** (по убыванию мягкости): (а) SET_DUTY 0 на каждый controller_id - спал к нулю, мотор под напряжением; (б) SET_CURRENT 0 - отпускание, если |ток| < `cc_min_current` (`mcpwm_foc.c:802-817`); (в) обёрнутая COMM-команда **COMM_MOTOR_ESTOP (159)** через `CAN_PACKET_PROCESS_SHORT_BUFFER`: `mc_interface_ignore_input_both(N мс)` + `mc_interface_release_motor_override_both()` - принудительное отпускание обоих моторов с игнором входа на N мс (`bldc@f2cd939 comm/commands.c:1678-1682`, enum `datatypes.h:1150`); (г) CAN_PACKET_SHUTDOWN (31) - питание-даун (только если HW поддерживает: `comm_can.c:1998-2009`). Тихий обрыв потока команд сам по себе через timeout_msec (дефолт 1000 мс) даёт brake-current 0 = отпускание.
- Дока рекомендует слать команды фиксированным темпом, напр. 50 Гц (`documentation/comm_can.md:18`) - V1 с ≥50 мс (≤20 Гц) укладывается в требования; запас до дефолтного 1000 мс.

## 4. Обработка чужих/неизвестных extended-ID; следствие для F4 (min-ID 0x1)

- RX-путь: extended-кадр → опциональный eid_callback приложения → `bms_process_can_frame` (потребляет BMS-типы 38-45/53-54 при дефолтном `MCCONF_BMS_TYPE = BMS_TYPE_VESC` и `BMS_FWD_CAN_MODE_DISABLED` - кадры поглощаются, наружу не форвардятся) → `decode_msg` (`bldc@f2cd239 comm/comm_can.c:1337-1368`; `bms.c:78-90, 105-110`; `motor/mcconf_default.h:636-637, 666-667`).
- В `decode_msg` однофреймовые команды исполняются **только если id получателя = 255 (broadcast), controller_id или second id** (`comm_can.c:1603-1605`: «addressed to this VESC or to all VESCs (id=255)»); статусные типы принимаются от любого отправителя и складываются в таблицы; прочие packet types - `default: break`, **молчаливое игнорирование без ACK/error-frame** (`comm_can.c:2024-2030`). VESC никогда не подтверждает SET_*-команды.
- **F4 (V3 min-ID force-stop EID 0x00000001, payload 0xFF×8)**: EID 0x1 = packet_type 0 (SET_DUTY) на controller_id 1. Приводы V1 имеют id 100/101 → кадр попадает в gate `id == 255 || id == id1 || id == id2` как чужой и **игнорируется молча**; payload 0xFF×8 для SET_DUTY к тому же декодировался бы как отрицательный duty (0xFFFFFFFF = -1.0 = полный реверс), если бы id совпал. UUID-derived дефолтные id пропускают зарезервированные {1,2,3,4,10,11} (`bldc@f2cd239 hwconf/hw.c:30-42`). **Вывод: force-stop 0x1 несовместим с VESC-прошивкой - привод не отреагирует.** VESC-нативные варианты broadcast-стопа: EID `(0 << 8) | 255 = 0x000000FF` с payload duty=0 (00000000) - принимается ВСЕМИ узлами шины; либо per-id SET_DUTY 0 на 100 и 101; либо обёрнутый COMM_MOTOR_ESTOP. Для закрытия F4 нужно решение: эмулятор стенда обязан имитировать выбранную V3 семантику (в т.ч. «не отвечать на 0x1», если V3 оставляет 0x1 как чисто внутренний), а реальная остановка приводов на полигоне должна идти VESC-совместимым кадром. Bench-проверка реакции живого привода на EID 0x1 - в списке UNKNOWN.
- Обнаружение узлов на шине: `CAN_PACKET_PING` (17) на конкретный id → `CAN_PACKET_PONG` (18) с [id, HW_TYPE_VESC] (`bldc@f2cd239 comm/comm_can.c:1839-1856`); чтение firmware/HW-имени через обёрнутый `COMM_FW_VERSION` (возвращает `HW_NAME` + UUID; у второго мотора dual-узла последний байт UUID +1: `comm/commands.c:231-258`).

## 5. Топология Dual: 2 узла CAN или 1 узел на 2 мотора

- Стандартный VESC-механизм dual-motor («HW_HAS_DUAL_MOTORS», STM32F4: STORMCORE 60D/100D, UNITY, Duet и др.): **один физический CAN-узел с ДВУМЯ моторными id: controller_id (мотор 1) и controller_id+1 (мотор 2)**, обёртка 255→0 (`bldc@f2cd239 util/utils_sys.c:67-77`). Команда, адресованная на `id2`, маршрутизируется в мотор-контекст 2: `decode_msg` выбирает motor thread по совпадению id (`comm/comm_can.c:1591-1605`); исходящие статусы шлются под обоими id (`send_can_status` шлёт status для controller_id и second id: `comm/comm_can.c:1470-1487`); петля `transmit_eid_replace` на собственные id не выходит на шину, а декодируется локально (`comm/comm_can.c:293-302`). Внутренний поток `cancom_status_internal_thread` кормит локальный декодер статусами второго мотора (`comm/comm_can.c:1447-1467`).
- **Пара V1 100/101 консистентна с ОДНИМ dual-узлом base id 100 (мотор 1 = движение id 100, мотор 2 = лифтер id 101)** - ровно паттерн id/id+1. Равноправно консистентна и с ДВУМЯ отдельными контроллерами, сконфигурированными на 100 и 101: внешнее поведение (SET_DUTY на 100/101, STATUS от 101) идентично.
- Vendor: «Dual FSESC75100» = dual-motor ESC, 14-84V (4-20S), 100A continuous на мотор / 200A dual, FOC/BLDC, CAN-порт, VESC_TOOL ([technobotix], [aliexpress]). Аппаратно два моста; upstream bldc содержит Flipsky hwconf только single-motor: `hwconf/flipsky/hw_75_100.h` («75_100», плата FOC 75100 V201) и `hw_75_100_V2.h` («75_100_V2») - без HW_HAS_DUAL_MOTORS (`bldc@f2cd239 hwconf/flipsky/`); официальный `flipsky_official/flipsky_75` («Flipsky_75») тоже single. Dual-версия прошивается форком Flipsky (vendor рекомендует заводскую fw 5.2; на 5.3+ требуется отключать phase filter - [technobotix], [aliexpress]) - **какой HW_NAME/топологию она репортит: UNKNOWN**.
- vesc_tool определяет dual-плату эвристикой по HW-имени (только «STORMCORE»/«UNITY»: `vesc_tool@2721f98 boardsetupwindow.cpp:402-409`) - т.е. сторонние dual-платы тулом не детектятся автоматически, это не признак их отсутствия.
- **Различающий bench-тест** (закрывает вопрос на сайте): прочитать `COMM_FW_VERSION` через CAN на id 100 и id 101 - у одного dual-узла оба id ответят одинаковым HW_NAME и UUID, отличающимся только в последнем байте на +1 (механика `bldc@f2cd239 comm/commands.c:252-256`); у двух отдельных контроллеров UUID полностью разные. Альтернативно: сменить id 100 и посмотреть, двигается ли 101 (у dual-узла второй id = base+1 всегда).

## 6. Контракт протокольного эмулятора стенда (сводка для #103)

- Физика: 500 kbit/s, extended frames (29-bit), VESC-режим. Никаких ACK от привода на SET_*-команды - эмулятор тоже не ACK-ает.
- RX от стенда (то, что эмулятор должен принимать): EID 100 и 101, packet 0 (SET_DUTY), DLC 4, BE int32 / 100000 → duty; диапазон V1: движение +0.03..+0.99 (и 0 = стоп), лифтер ±0.05..±0.5, знак = направление. Прочие EID игнорируются молча (в т.ч. EID 0x1 - если V3 сохраняет min-ID force-stop, эмулятор обязан его игнорировать как VESC).
- TX эмулятора: EID 2405 (STATUS от id 101), DLC 8, BE: B0-3 ERPM (int32, масштаб 1 - для лифтера может быть 0/спокойный профиль, V1 не читает), **B4-5 ток мотора int16 × 0.1 А** (V1 семплит только в `lifter_Up`, порог 500 = 50 А, зажим 250 = 25 А), B6-7 duty × 0.001. Частота: у реального привода - по appconf (дефолт потока 1 = 50 Гц при включённом бит0 msgs_r1; фактическое значение привода UNKNOWN); эмулятору достаточно темпа выше частоты опроса V1 с запасом.
- Модель поведения: duty 0 → спал к нулевой модуляции (мотор «удерживается» в RUNNING); тишина команд > timeout (дефолт 1000 мс; фактический UNKNOWN) → brake current (дефолт 0) → отпускание/выбег; эмулятор должен отразить выбранный V3 сценарий F4 (см. п. 4).
- Живость/обнаружение: эмулятор может (опционально) отвечать PONG (packet 18, [id, 0]) на PING (17) на id 100/101 - для процедур commissioning стенда.

## Сводка UNKNOWN (bench / сайт-чек)

1. Фактические `controller_id` приводов (ожидание 100/101 по V1-кадрам; проверка PING/командой).
2. `timeout_msec` / `timeout_brake_current` / `kill_sw_mode` каждого привода (bench read appconf; дефолты кода 1000 мс / 0.0 / disabled).
3. Включение и скорость статусного вещания лифтера (`can_status_msgs_r1/r2`, rates; дефолты 0/0/50/5 - вещание по умолчанию выключено, у лифтера явно включено).
4. HW_NAME и версия прошивки dual-узла (форк Flipsky; vendor - fw 5.2; bench `COMM_FW_VERSION`), актуальность предупреждения про phase filter на fw ≥ 5.3.
5. Топология: один dual-узел (id 100 = base) vs два отдельных контроллера (bench-тест UUID в п. 5).
6. Реакция живого привода на EID 0x00000001 (F4 #58: прогноз «молчаливый игнор», нужна bench-подтверждение перед фиксацией V3-семантики force-stop).
7. Направление вращения от знака duty на конкретном моторе (полярность фаз/`m_invert_direction`) - для эмулятора не критично, для полигона да.

## Bench-чеклист (как прочитать конфигурацию привода)

- Через VESC Tool (USB) или CAN: `COMM_GET_APPCONF` (обёртка `CAN_PACKET_PROCESS_SHORT_BUFFER`, поля `controller_id`, `timeout_msec`, `timeout_brake_current`, `can_status_msgs_r1/r2`, `can_status_rate_1/2`, `can_baud_rate`) и `COMM_FW_VERSION` (HW_NAME, UUID).
- CAN-скан живости: `CAN_PACKET_PING` на 0..254, ждём PONG (10 мс таймаут в `comm_can_ping`: `bldc@f2cd239 comm/comm_can.c:646-676`).
- Тест тока: подать SET_DUTY на 101 при заблокированном лифтере и снять B4-5 STATUS 2405 - калибровка порога 500 под реальный моментный ток.
