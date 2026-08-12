<!-- markdownlint-disable MD013 MD060 MD024 MD036 -->
<!--
MD024/MD036: намеренная повторяющаяся таксономия subsections («Entry points и call sites», «Admission/preconditions» и т.д.) одинакова для всех 13 operations и оформлена жирными маркерами; документ прошёл независимый checklist-review в этой форме (issue 14). Структура сохраняется как утверждённый evidence asset.
-->

# Capability Evidence Slices каталога операций V3 (V1 production evidence bundle)

## Evidence metadata

Канонический asset нормативного пакета (введён тикетом «Ввести Capability Evidence Slices в нормативный пакет», issue 53). Формат evidence по правилу issue 6: source / version / confidence.

- **Source**: production-код канонического репозитория `Driadix/ShuttleController` ветки `main` (evidence SHA `708d090980155d4a8d4644f7bcf87c383e81cd1d`); локальное зеркало `C:\Projects\Shuttle\ShuttleController` (HEAD `4a226e5`, исходники идентичны evidence SHA).
- **Version**: bundle revision `8dd3d0c` -> `70d78ec` (ветка `research/v3-capability-evidence-slices`, перенесена в пакет issue 53); независимый checklist-review по gate `legacy evidence ready` пройден, 5 minor-правок anchors внесены, блокирующих замечаний нет.
- **Confidence**: факты разделены по классам `configured`/`inferred` в тексте; measured worst-case bounds не заявляются (см. [v1-execution-evidence](./v1-execution-evidence.md)); disposition `preserve/change/exclude/unknown` - предложения, не норматив до verification gate владельца.
- **Trace model (issue 5/12)**: при построении репозиторной схемы пакета (вариант B, issue 12) разделы bundle станут источником `EVD` Trace Records с типизированными ID; физический формат записей остаётся validation obligation схемы, здесь не предвосхищается.
- **Retention obligation (commit-pinned links)**: выполнена (issue 55) — системный индекс и execution evidence промоутнуты в `main` (`docs/research/v1-system-evidence-index.md`, `docs/research/v1-execution-evidence.md`), ссылки ниже переведены на относительные; research-ветки `research/v1-system-evidence` и `research/v1-execution-evidence` больше не являются источником доступа к evidence и могут быть удалены без 404 (provenance-требование issue 5 соблюдено).

## Research snapshot

Этот asset отвечает на GitHub issue 14 карты «Спроектировать спецификацию и план прошивки контроллера V3» и использует только production-код канонического репозитория [`Driadix/ShuttleController@708d090980155d4a8d4644f7bcf87c383e81cd1d`](https://github.com/Driadix/ShuttleController/tree/708d090980155d4a8d4644f7bcf87c383e81cd1d).
Локальное зеркало на момент исследования: `C:\Projects\Shuttle\ShuttleController` (HEAD `4a226e5` отличается от evidence SHA только добавленным `docs/Controller-Nonblocking-Refactoring-Plan.md`; `git diff 708d090..HEAD -- Cntrl_V2/` пуст). Все source anchors сняты с working tree, идентичной evidence SHA по исходникам, и проверены чтением диапазонов.
Не использованы как evidence: `docs/Controller-Nonblocking-Refactoring-Plan.md`, refactor-ветки, `tests/`, артефакты предыдущего рефакторинга.
V1 - описательное свидетельство, а не норматив для V3: каждый факт сопровождается предлагаемой disposition `preserve / change / exclude / unknown`, которая становится решением только в verification gate владельца.

Формат anchors: `Cntrl_V2/Cntrl_V2.ino:L<start>-L<end>` относительно корня репозитория; permalink-эквивалент - `https://github.com/Driadix/ShuttleController/blob/708d090980155d4a8d4644f7bcf87c383e81cd1d/<path>#L<start>-L<end>`.

## Scope и тип slice

Bundle покрывает все 13 root operation types каталога V3, утверждённого в issue 9: `MoveDistance`, `LiftTo`, `LoadPallet`, `UnloadPallet`, `LongLoad`, `LongUnload`, `LongUnloadQuantity`, `CompactPallets`, `CountPallets`, `Home`, `Calibrate`, `Demo`, `Evacuate`.

Тип slice: каталогный bundle - набор срезов по доменному поведению «один внешний operation request и полный observable lifecycle принятой операции» плюс переиспользуемые suboperations/primitives (motion, lifter, pallet detection), описанные раздельно и связанные ссылками.

Внешние акторы:

- Control Client через display UART (`Serial1`, 230400 8E1) и radio UART (E22, 57600) - общий framed-протокол, source-зависимый admission.
- Оператор/погрузчик, физически подающий и забирающий паллеты в канал - источник «паллета появилась/исчезла» для long-операций и load/unload; в V1 наблюдается только через датчики.

Явные исключения scope:

- `CMD_LOG_MODE` исключён из каталога (issue 9); в V1 объявлен в wire enum, но отвергается парсером.
- Непрерывные ручные движения `CMD_MOVE_LEFT_MAN`/`CMD_MOVE_RIGHT_MAN` - manual session (intent/lease), не root operation; зафиксированы только как контекст shared primitives.
- Сервисные и lifecycle-операции (`RestartController`, `ClearFault`, `PersistConfiguration`, `UpdateFirmware`, get-config) классифицированы в issue 9 отдельно и в этот bundle не входят.
- Совместимость wire protocol с V1/V7 не требуется (решение карты); enum-значения зафиксированы только как факт V1.

## Связь с каноническими cross-cutting evidence items

Общие факты живут в канонических items системного индекса и execution evidence; этот bundle фиксирует только их локальное применение:

- [Системный индекс свидетельств production-кода V1](./v1-system-evidence-index.md) (issue 3): command/operation surface, двойная state representation (`status`/`currentOperation`/`CoreOpMode`), shared I2C/CAN ownership, configuration/persistence, профили 800/1000/1200, faults/warnings/recovery, blocking-работа.
- [V1 execution timing and resource evidence](./v1-execution-evidence.md) (issue 11): items `T01-T13`, `C01-C04` - configured/inferred тайминги и отсутствие measured worst-case bounds; кооперативный `SystemYield()` как единственный watchdog/safety/communication pump.
- [Общий semantic contract операций V3](https://github.com/Driadix/ShuttleControllerV3/issues/13) - нормативный контекст V3; evidence ниже не переопределяет его, только сверяется с ним в disposition.

Дельты, найденные этим bundle относительно системного индекса, фиксируются в разделах операций явно (например: недостижимость branch-а `WARN_CHANNEL_FULL` в одиночной загрузке из-за обнуления `lastPalletePosition` при приёме команды; `CMD_EVACUATE_ON` отвергается парсером - production behavior отсутствует).

## Общие entry points и dispatch skeleton

Все операции каталога имеют один admission-скелет (anchors ниже проверены):

1. Приём кадра: `pollSerial` из `SystemYield()` для display и radio (`Cntrl_V2/Cntrl_V2.ino:L6950`, `L6954`) -> `processPacket()` (`L2713`).
2. `isSupportedCommand(reqCmd)` (`L2782`) - иначе `ACK_REJECTED` (`L2784`); состав supported-списка `L8351-L8384`.
3. Provisioning: непривязанный шаттл допускает только STOP/STOP_MANUAL/SYSTEM_RESET/RESET_ERROR (`L8406-L8409`) - иначе `ACK_BAD_ENVIRONMENT` (`L2819-L2828`).
4. Active fault: только override-команды (`L8418-L8423`) проходят при `isErrorActive()` - иначе `ACK_ERROR_STATE` (`L2832-L2841`).
5. In-channel: `digitalRead(CHANNEL)` (`L2830`); exempt-список `L8411-L8416` (stop/reset/save/get-config/lift) - остальные вне канала получают `WARN_NOT_IN_CHANNEL` + `ACK_BAD_ENVIRONMENT` (`L2843-L2853`).
6. Занятость: `canAcceptCommandNow` (`L8430-L8463`) - для большинства операций каталога fallback `isShuttleIdle()` (`L8425-L8428`); иначе `ACK_BUSY` (`L2874-L2884`). Lift-команды имеют более строгое правило, distance-команды допускают MANUAL.
7. `ACK_OK` (`L2871`) с radio telemetry override `predictTelemetryStateForAcceptedCommand` (`L8271-L8292`); повторная проверка `canAcceptCommandNow` при присвоении `status` в `SystemYield` (`L6975-L7039`).
8. `loop()` IDLE: повторный тройной опрос CHANNEL (`L1829-L1833`), `currentOperation = mapCmdToOperation(status)` (`L1843`), `lastPalletePosition = 0` (`L1845`), `currentMode = AUTO_EXEC` (`L1870`), `run_Cmd()` (`L1898-L1900`), завершение `if (status != CMD_STOP) status = 0; currentOperation = STATE_IDLE; currentMode = IDLE` (`L1902-L1907`).

Dispatch-таблица `run_Cmd()` для каталога: `Cntrl_V2/Cntrl_V2.ino:L3228-L3397` (LIFT `L3244-L3257`, LOAD `L3265-L3272`, UNLOAD `L3273-L3283`, MOVE_DIST_R/F `L3284-L3297`, CALIBRATE `L3298-L3307`, DEMO `L3308-L3312`, COUNT_PALLETS `L3313-L3324`, COMPACT_F `L3330-L3338`, COMPACT_R `L3339-L3348`, LONG_LOAD `L3349-L3358`, LONG_UNLOAD `L3359-L3373`, LONG_UNLOAD_QTY `L3374-L3390`, HOME `L3391-L3395`).

Общие исходы (behavior map верхнего уровня):

- Normal: операция выполняется синхронно внутри `run_Cmd()`; финал `status = CMD_STOP`, `send_Cmd()`, переход в IDLE.
- Rejection: `ACK_REJECTED/BAD_ENVIRONMENT/ERROR_STATE/BUSY` до исполнения; операция не создаётся.
- Stop/cancellation: `CMD_STOP`/`CMD_STOP_MANUAL` принимаются `SystemYield()` в любой момент (`L6962-L6974`, немедленный `motor_Stop()`); циклы операций проверяют `shouldAbortLoop()` (`L8598-L8601`) и завершаются через `preserveManualStopOnAbort()` (`L8585-L8591`) либо принудительный `status = CMD_STOP`.
- Fault: любой active fault -> `isErrorActive()` -> abort + `CoreOpMode::ERROR` на следующей итерации loop (`L1819-L1820`) с принудительным `motor_Force_Stop()` при не-stationary (`L2050-L2058`); в ERROR доступны только reset/reset-error/save.
- Recovery: sensor/bus recovery (ToF bus, AS5600, radio) - cross-cutting сервисы `SystemYield()`; auto-clear faults только при физической неподвижности (системный индекс, раздел Faults/recovery).

Shared primitives, описанные в bundle отдельными разделами: `motor_Speed`/`motor_Start_Forward/Reverse`/`motor_Stop`/`motor_Force_Stop` (CAN ID 100), `lifter_Up`/`lifter_Down`/`lifter_Stop` (CAN ID 101, ток ID 2405), `moove_Forward`/`moove_Reverse` (condition-driven loops без общего deadline), `moove_Distance_F/R` (AS5600-интеграция, no-progress watchdog), `get_Distance`/`set_Position` (ToF round-robin, AS5600), `detect_Pallete` (4 GPIO с debounce), `blink_Work` (stall-детект 1500 ms).

## Профильные варианты 800/1000/1200 - сводка

`shuttleLength` конфигурируется без валидации набора значений (`Cntrl_V2/Cntrl_V2.ino:L2948-L2953`, default 1000 `L598`). Специальные ветки в scope каталога:

- Останов перед паллетой: `-25` только для 1000/1200 (`L4397-L4398`, `L4627-L4628`).
- Заезд под паллету `dst`: 600 базово, 670 для 1200 (`L5724-L5726`); в unload 500 для 800 у конца канала (`L5382-L5386`).
- Board-delay `maxbb`: -3 шага для 1200 (`L5392-L5402`, `L5734-L5744`).
- Recapture: 100/250/450 мм (`L5493-L5497`, `L5864-L5869`).
- 800: single-load требует обе пары pallet-сенсоров, иначе `WARN_PALLET_SIZE_ERROR` (`L5949-L5956`).
- Движение/лифтер/калибровка/home профильных ветвлений не имеют.

---

## Группа: MoveDistance + LiftTo и motion/lifter примитивы

Источник: `C:/Projects/Shuttle/ShuttleController` (локальное зеркало). Evidence SHA: `708d090980155d4a8d4644f7bcf87c383e81cd1d`. HEAD зеркала `4a226e5` отличается только `docs/Controller-Nonblocking-Refactoring-Plan.md` (не используется). Все anchors ниже прочитаны и сверены с working tree. Файлы: `Cntrl_V2/Cntrl_V2.ino` (далее `.ino`), `Cntrl_V2/ShuttleProtocol.h` (далее `SP.h`), `Cntrl_V2/AlertManager.h`, `Cntrl_V2/TofHealthMonitor.h/.cpp`.

Cross-cutting факты (SystemYield, shouldAbortLoop, ACK-коды, status/currentOperation, motor_Force_Stop, ToF freshness gate, bumper IRQ/handlePendingCrash) считаются установленными; здесь фиксируется только их локальное применение в scope.

Глобальные состояния (`.ino:565-567`): `uint8_t status = 0` (текущая команда/stop-сигнал), `ShuttleState currentOperation = STATE_IDLE` (внешний lifecycle). Направление/стопорение движения: `motorStart` (`.ino:604`), `motorReverse` (`.ino:605`, 0=вперёд, 1=назад, 2=остановлено). `inverse` (`.ino:607`) - флаг инверсии направления, сохраняемый в EEPROM; меняет местами mapping направлений и mapping ToF-сенсоров/слотов distance[].

---

### 1. MoveDistance (CMD_MOVE_DIST_F = 0x13, CMD_MOVE_DIST_R = 0x12)

#### Entry points и call sites

- Константы команд: `SP.h:99-100` (`CMD_MOVE_DIST_R = 0x12`, `CMD_MOVE_DIST_F = 0x13`, комментарий "Requires MSG_CMD_WITH_ARG"); `MSG_CMD_WITH_ARG = 0x31` (`SP.h:62`), пакет `ParamCmdPacket { int32_t arg; uint8_t cmdType; }` (`SP.h:367-371`).
- В supported-списке: `isSupportedCommand` (`.ino:8351-8384`), обе команды на строках 8364-8365. Хелпер `isManualDistanceCommand` (`.ino:8391-8394`).
- Dispatch-путь (автономный): SystemYield опрашивает UART'ы (`pollSerial(SerialDisplay...)` `.ino:6950`, `pollSerial(SerialLora...)` `.ino:6954`) → `processPacket` (`.ino:2713`) → ветка `MSG_CMD_SIMPLE || MSG_CMD_WITH_ARG` (`.ino:2777`) → возврат `reqCmd` (`.ino:2872`) → `status = cmdRad` (`.ino:6987`) или `status = cmdDisp` (`.ino:7018`) → `loop()` case `CoreOpMode::IDLE` (`.ino:1824-1896`) → `currentMode = CoreOpMode::AUTO_EXEC` (`.ino:1867-1871`) → `run_Cmd()` (`.ino:1900`) → ветки `CMD_MOVE_DIST_R` (`.ino:3284-3290`) / `CMD_MOVE_DIST_F` (`.ino:3291-3297`): `send_Cmd(); moove_Distance_X(mooveDistance); status = CMD_STOP; send_Cmd();`.
- Dispatch-путь (ручная сессия): case `CoreOpMode::MANUAL`, ветка `CMD_MOVE_DIST_R || CMD_MOVE_DIST_F` (`.ino:1921-1952`) → `moove_Distance_F(mooveDistance)` (`.ino:1934`) / `moove_Distance_R(mooveDistance)` (`.ino:1938`).
- Физические функции прямо: `moove_Distance_F(int)` (`.ino:4796-4800`) → `moove_Distance_F(dist, 100, 10)` + `motor_Stop()`; `moove_Distance_R(int)` (`.ino:4997-5001`) аналогично. Тело: `moove_Distance_F(int,int,int)` (`.ino:4803-4994`), `moove_Distance_R(int,int,int)` (`.ino:5004-5194`).
- Косвенно: `get_Distance` (`.ino:3857-3919`), `readAs5600AngleForMotion` (`.ino:1052-1068`), `motor_Start_Forward` (`.ino:2239-2247`) / `motor_Start_Reverse` (`.ino:2250-2258`), `motor_Speed` (`.ino:2088-2236`), `motor_Stop` (`.ino:2261-2334`), `set_Position` (`.ino:3922-4034`), `blink_Work` (`.ino:4051` и далее), `lifter_Down` (`.ino:2453-2521`, только в fault-пути), `preserveManualStopOnAbort` (`.ino:8585-8591`), `ensureChannelTofReadyForMotion` (`.ino:3536-3553`, через motor_Start_*), `SystemYield` (`.ino:6839-7058`), `shouldAbortLoop` (`.ino:8598-8601`).

#### Admission/preconditions

Все проверки в `processPacket`, ветка MSG_CMD (`.ino:2777-2884`), в порядке исполнения:

1. `isSupportedCommand(reqCmd)` (`.ino:2782`) → иначе `ACK_REJECTED` (`.ino:2784`).
2. Provisioning: `!isProvisionedShuttle() && !isUnprovisionedCommandAllowed(reqCmd)` (`.ino:2819`) → `ACK_BAD_ENVIRONMENT` (`.ino:2826`); для distance-команд логируется `Manual reject cmd=%02X reason=not_prov` (`.ino:2820-2825`). `isUnprovisionedCommandAllowed` = только STOP/STOP_MANUAL/SYSTEM_RESET/RESET_ERROR (`.ino:8406-8409`).
3. `isErrorActive() && !isOverrideCommand(reqCmd)` (`.ino:2832`) → `ACK_ERROR_STATE` (`.ino:2839`).
4. `inChannel = digitalRead(CHANNEL)` (`.ino:2830`, пин PB5 - `.ino:374`); `!inChannel && !isOutOfChannelExemptCommand(reqCmd)` (`.ino:2843`) → `WARN_NOT_IN_CHANNEL` (5000 ms) + `ACK_BAD_ENVIRONMENT` (`.ino:2848-2851`). Distance-команды НЕ exempt (`isOutOfChannelExemptCommand` `.ino:8411-8416`), т.е. движение на дистанцию вне канала отвергается.
5. `canAcceptCommandNow(reqCmd, replyPort == &SerialLora)` (`.ino:2855`): для distance-команд `currentMode == MANUAL || isShuttleIdle()` (`.ino:8447-8450`), где `isShuttleIdle()` = `status == 0 || status == CMD_STOP` (`.ino:8425-8428`); иначе `ACK_BUSY` (`.ino:2881`).
6. Аргумент: `mooveDistance = cmdArgs->arg` (`.ino:2861`), `mooveDistance` - `uint16_t` (`.ino:596`), `arg` - `int32_t` (`SP.h:369`): truncation, валидации диапазона нет.
7. `ACK_OK` (`.ino:2871`), для radio с telemetry вычисляется `predictTelemetryStateForAcceptedCommand` (`.ino:2866-2869`, определение `.ino:8271-8292`).
Повторная проверка канала при старте из IDLE: тройное чтение CHANNEL с `delay(5)` (`.ino:1829-1833`), при провале - `WARN_NOT_IN_CHANNEL`, `status = 0` (`.ino:1835-1840`).
Дополнительные пред-условия внутри самой операции: `distance[1] >= 70` для F (`.ino:4806`) / `distance[0] >= 70` для R (`.ino:5007`); успешное чтение AS5600 (`readAs5600AngleForMotion`, `.ino:4813-4816` / `.ino:5014-5017`); ToF freshness gate в `motor_Start_Forward/Reverse` (`.ino:2241-2244` / `.ino:2252-2255`), bypass при `sensorOff` (`.ino:3538-3541`).

#### Шаги и переходы (normal path, F; R - зеркально с slot 0)

1. `get_Distance()` (`.ino:4805`); gate `distance[1] < 70` → `LOG_WARN "End of channel F, can't moove..."` и return без движения (`.ino:4806-4809`).
2. `LOG_INFO "Moove F distance = %d Pos = %d"` (`.ino:4811`); чтение стартового угла AS5600 `readAs5600AngleForMotion(&startAngle)` (`.ino:4813`); при неудаче - latch `FAULT_AS5600` (через `latchAs5600Fault("motion_read", ...)` в `.ino:1052-1068`) и return.
3. Клампизация параметров: `maxSpeed < 2 → 2` (`.ino:4820-4821`); `minSpeed > maxSpeed → maxSpeed-1` (`.ino:4822-4823`, ДО dist-cap'ов - см. Unknowns/ordering); dist-капы: `(500,1000]→50`, `(300,500]→30`, `<=300→20` (`.ino:4824-4829`); редукция дистанции: `dist>500 → -50`, `dist>50 → -10` (`.ino:4830-4833`).
4. `motor_Start_Forward()` (`.ino:4834`): ToF freshness gate для forward-слота; при успехе `motorStart=1, motorReverse=0`. `get_Distance()` (`.ino:4835`).
5. Цикл `while (moove)` (`.ino:4838`): каждый оборот - `SystemYield()` (`.ino:4840`), `shouldAbortLoop()` → `preserveManualStopOnAbort()` + `motor_Stop()` + return (`.ino:4841-4846`), `blink_Work()` (`.ino:4847`), `get_Distance()` (`.ino:4848`).
6. Управляющий тик каждые 50 ms: `if (millis() - count > 50)` (`.ino:4849`): `set_Position()` (`.ino:4851`); повторное чтение AS5600 (`.ino:4852`); при неудаче - `motor_Stop(); status = CMD_STOP; return` (`.ino:4853-4856`).
7. Интеграция пройденного пути: `diff` вычисляется по разности углов с 4 wrap-кейсами, калибровочные коэффициенты `calibrateEncoder_F[8]` при `!inverse`, иначе `calibrateEncoder_R[8]` (`.ino:4858-4935`; секторы по 512 единиц из 4096); `diff < 0 → 0` (`.ino:4933-4934`); при `diff != 0`: `dist -= diff; startAngle = currentAngle; cnt = millis()` (`.ino:4935-4940`) - сброс no-progress таймера.
8. Выбор скорости (`.ino:4942-4975`):
   - `distance[1] >= maxSpeed*15 && dist >= maxSpeed*15` → `motor_Speed(maxSpeed)`;
   - `distance[1] <= 90 + chnlOffset` → `motor_Stop(); moove = 0; currentPosition = 0` (конец канала, `.ino:4946-4951`);
   - `dist >= distance[1] && distance[1] < maxSpeed*15` → `motor_Speed(distance[1]/15)`;
   - `dist < maxSpeed*15 && dist > minSpeed*15` → `motor_Speed(dist/15)`;
   - `dist <= minSpeed*15` → minSpeed/oldSpeed-логика (`.ino:4960-4972`);
   - `dist <= 0` → `moove = 0` (`.ino:4973-4975`).
9. Таймаутные выходы (только внутри 50 ms тика): `lifterUp && millis()-cnt > 3000 && dist < 30` → `motor_Stop(); return` БЕЗ fault (`.ino:4977-4980`); иначе `millis()-cnt > 5000` → `setFault(FAULT_MOVE_TIMEOUT); motor_Stop(); lifter_Down(); status = CMD_STOP; return` (`.ino:4982-4988`).
10. `count = millis()` (`.ino:4990`); после цикла `LOG_DEBUG "End mooving"` (`.ino:4993`). Одноаргументный wrapper добавляет `motor_Stop()` после возврата (`.ino:4799`/`.ino:5000`) - останавливает мотор, если цикл вышел по `moove = 0` (доехали/конец канала/`dist<=0`) без явного motor_Stop внутри.
11. Завершение в `run_Cmd`: `status = CMD_STOP; send_Cmd()` (`.ino:3288-3289`/`.ino:3295-3296`); AUTO_EXEC: `if (status != CMD_STOP) status = 0; currentOperation = STATE_IDLE; currentMode = IDLE` (`.ino:1902-1907`).

Выходы из цикла: (а) `dist <= 0` (доехали); (б) конец канала `distance[slot] <= 90+chnlOffset`; (в) abort (stop/ошибка); (г) AS5600 read fail; (д) no-progress timeout; (е) lifted near-target escape. Общего deadline у операции нет.

#### Stop/fault/abort

- CMD_STOP/CMD_STOP_MANUAL, пришедшие во время движения, обрабатываются в `SystemYield` (вызывается в каждой итерации цикла операции): статус выставляется в `.ino:6962-6974` (включая немедленный `motor_Stop()` в самом SystemYield), далее цикл операции видит `shouldAbortLoop()` → `preserveManualStopOnAbort()` (сохраняет CMD_STOP_MANUAL, иначе ставит CMD_STOP, `.ino:8585-8591`) + `motor_Stop()` + return.
- Fault во время исполнения: любой active fault делает `shouldAbortLoop()` истинным (через `isErrorActive()`, `.ino:8593-8596`) → тот же abort-путь. `FAULT_MOVE_TIMEOUT` выставляется сама операция (`.ino:4984`/`.ino:5184`), после чего принудительно опускает лифт `lifter_Down()` (`.ino:4986`/`.ino:5186`).
- Отказ AS5600: read fail в тике → `motor_Stop(); status = CMD_STOP; return` (`.ino:4853-4856`); read fail на старте → latch FAULT_AS5600 и тихий return (`.ino:4813-4816`).
- Отказ ToF: gate на старте (`motor_Start_*`) latch-ит fault → abort на следующей итерации; во время движения - `enforceActiveMotionTofSafety` в `get_Distance` → `motor_Force_Stop()` (`.ino:3555-3571`).
- Если `motor_Start_*` не выставил флаги из-за gate'а (fault уже latch-ится), цикл всё равно стартует, но `motor_Speed` no-op при `motorReverse == 2` (`.ino:2090`) → операция завершается через abort (fault active) на следующем обороте.
- Пути с SystemYield: каждый оборот `while (moove)` и каждый accel/decel-шаг `motor_Speed`. Пути без SystemYield: `motor_Stop` (блокирующие `delay()`, `.ino:2289-2294`, `.ino:2307-2326`), `delay(5)` в IDLE-приёмке (`.ino:1830-1832`).

#### Observable outcomes

- Успех: `status = CMD_STOP` (run_Cmd, `.ino:3288`), `currentOperation` → `STATE_IDLE`, `currentMode` → IDLE (`.ino:1902-1907`); финальный `send_Cmd()`; `LOG_DEBUG "End mooving, position = %d"` (`.ino:4993`).
- Ручная сессия: по успеху `status = CMD_MANUAL_MODE`, `currentOperation = STATE_MANUAL`, `LOG_INFO "Manual step done -> ready"`, `send_Cmd()` (`.ino:1940-1946`); при abort - только `LOG_WARN "Manual step abort status=%02X"` (`.ino:1950`).
- Telemetry state при приёмке: `STATE_MOVE_FWD = 13` / `STATE_MOVE_REV = 14` через `mapCmdToOperation` (`.ino:8251-8254`; значения `SP.h:160-161`).
- Stats: `sramStats->payload.totalDist += diff` в `set_Position` (`.ino:3965-3976`, `.ino:4013-4024`).
- Faults/warnings, которые может latch-ить путь: `FAULT_MOVE_TIMEOUT` (`.ino:4984`/`.ino:5184`, `SP.h:183`, бит 1<<13), `FAULT_AS5600` (через `readAs5600AngleForMotion`), `FAULT_TOF_CH_F/FAULT_TOF_CH_R` (через gate/`enforceActiveMotionTofSafety`, `latchTofMeasurementFault` `.ino:3505-3534`), `FAULT_MOTOR_STALL` (через `blink_Work`, `.ino:4078-4085`, если позиция не меняется >=1500 ms при motorStart&&oldSpeed), `WARN_NOT_IN_CHANNEL` (`.ino:2848`/`.ino:1838`).
- Логи: `LOG_INFO "Moove F/R distance = %d Pos = %d"` (`.ino:4811`/`.ino:5012`), `LOG_WARN "End of channel F/R, can't moove..."` (`.ino:4807`/`.ino:5008`), `LOG_INFO "Motor stop, speed = %d"` в motor_Stop (`.ino:2266`).
- Побочные эффекты на глобальные переменные: F-конец канала - `currentPosition = 0` (`.ino:4950`); R-конец канала - `channelLength = currentPosition + shuttleLength` (`.ino:5152`).

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| 50 ms | `.ino:4849`, `.ino:5050` | configured | период управляющего тика (позиция/скорость) в цикле операции |
| 50 ms | `.ino:2090-2092` | configured | минимальный интервал между записями скорости в CAN (`countMoove`) |
| 5000 ms | `.ino:4982`, `.ino:5182` | configured | no-progress timeout (нет дельты AS5600) → FAULT_MOVE_TIMEOUT |
| 3000 ms при `dist < 30` и `lifterUp` | `.ino:4977`, `.ino:5177` | configured | escape без fault: поднятая платформа, нет прогресса у цели |
| 70 mm | `.ino:4806`, `.ino:5007` | configured | минимальная дистанция до конца канала для старта |
| `90 + chnlOffset` mm | `.ino:4946`, `.ino:5148` | configured | порог конца канала (chnlOffset - int8_t из EEPROM, `.ino:617`) |
| капы maxSpeed 50/30/20 | `.ino:4824-4829`, `.ino:5021-5026` | configured | по диапазонам dist (500..1000], (300..500], <=300 |
| редукция dist -50/-10 | `.ino:4830-4833`, `.ino:5027-5030` | configured | компенсация выбега: dist>500 → -50; dist>50 → -10 |
| множитель 15 (mm на единицу скорости) | `.ino:4942-4958` | inferred | тормозной путь = скорость*15; физический смысл из source не устанавливается |
| 2000 (единиц угла) | `.ino:4864-4935` | configured | порог различения wrap-направлений в интеграции угла |

#### Resource effects

- CAN ID 100 (extended, len 4): команды скорости движения; big-endian int32 = скорость * 1000 (`.ino:2096-2121`; `cracked_int_t` union int/bytes `.ino:464-469`); Can1 500 kbit/s, extended frames (`.ino:1735-1737`). Знак: `hexSpeed.vint = -hexSpeed.vint` при `motorReverse ^ inverse` (`.ino:2114-2115`).
- I2C1-устройства: TOF-сенсоры (через `get_Distance`/`TOF_Inquire_I2C_Decoding_ByID`, `.ino:3887`) и AS5600 (`as5600Sensor.readAngle`, `.ino:1057`).
- UART: `send_Cmd()` (телеметрия/ACK) до и после операции (`.ino:3285-3289`); дренаж command-портов в SystemYield на каждом обороте.
- GPIO: чтение CHANNEL (PB5) при приёмке (`.ino:2830`); светодиоды через `blink_Work` (GREEN_LED PC1, WHITE_LED PC2, BOARD_LED PA1 - `.ino:361-364`).
- Flash: не пишется. Delays: `delay(5)`x2 в IDLE-приёмке (`.ino:1830-1832`); блокирующие delay в `motor_Stop` (`.ino:2289-2294`, `.ino:2307-2326`).

#### Профильные варианты 800/1000/1200

В самом MoveDistance профильных ветвлений нет. `shuttleLength` (default 1000, `.ino:598`) участвует косвенно:

- R-конец канала: `channelLength = currentPosition + shuttleLength` (`.ino:5152`);
- `set_Position`: нижняя граница `channelLength >= currentPosition + shuttleLength` (`.ino:4032-4033`).
Задание: `CFG_SHUTTLE_LEN` без валидации диапазона (`.ino:2948-2953`), full-config `shuttleLen` (`.ino:3049-3050`), EEPROM load (`.ino:7631`). Сравнения `shuttleLength == 800/1000/1200` в scope операции отсутствуют (встречаются в unload/compact-путях вне scope, напр. `.ino:5383-5385`).

#### Unknowns

- Физическая размерность аргумента dist: из source следует мм-подобная (пороги 70/90/30, капы), но явно не документирована; коэффициент 15 не выводим из source.
- Поведение при `arg <= 0`: `dist<=0` → выход по `moove=0` на первом же тике (после wrapper-`motor_Stop`); при отрицательном `arg` - wrap в большое uint16 (truncation `.ino:2861`, валидации нет) - фактическое поведение при arg > 65535/отрицательном не специфицировано.
- Калибровочные массивы `calibrateEncoder_F/R` (default {40..40}, `.ino:569-574`) - как заполняются в production, в scope не устанавливается.
- Почему именно `dist < 30` и 3000 ms для lifted-escape - не устанавливается.

#### Disposition proposals (предложения, не решения)

- V1-факт: аргумент trunc-ится int32→uint16 без валидации (`.ino:2861`, `.ino:596`). Предложение: change - валидировать диапазон и отвергать ACK_REJECTED. Обоснование: отрицательные/сверхбольшие значения дают неопределённое поведение операции.
- V1-факт: no-progress timeout 5000 ms → FAULT_MOVE_TIMEOUT + принудительный `lifter_Down()` (`.ino:4982-4988`). Предложение: preserve концепцию таймаута; change - автоматическое опускание лифта в fault-пути требует отдельного решения (операция смены состояния платформы как побочный эффект fault).
- V1-факт: lifted near-target escape 3000 ms без fault (`.ino:4977-4980`). Предложение: change/exclude - тихое допущение может маскировать упор в препятствие с грузом.
- V1-факт: dist-капы maxSpeed и редукция dist (`.ino:4824-4833`). Предложение: preserve как tuning-политику; константы пересмотреть.
- V1-факт: порядок клампизации в F: `minSpeed>maxSpeed` проверяется ДО dist-капов (`.ino:4822-4829`), в R - после (`.ino:5021-5034`); в F после капа возможно `minSpeed > maxSpeed`. Предложение: change - унифицировать с R-порядком (ordering-баг).
- V1-факт: конец канала F обнуляет `currentPosition` (`.ino:4950`), R пересчитывает `channelLength` (`.ino:5152`). Предложение: preserve семантику координат, change - явная привязка к профилю длины.
- V1-факт: интеграция пути по AS5600 с 8-секторной калибровкой (`.ino:4858-4935`). Предложение: preserve концепцию; unknown - нужны ли сектора в V3 при ином датчике.
- V1-факт: ToF freshness gate на старте и force-stop при движении (`.ino:3536-3571`). Предложение: preserve.

---

### 2. LiftTo (CMD_LIFT_UP = 0x14, CMD_LIFT_DOWN = 0x15)

#### Entry points и call sites

- Константы: `SP.h:101-102`. Supported-список: `.ino:8366-8367`; хелпер `isLiftCommand` (`.ino:8401-8404`).
- Dispatch автономный: тот же parsing-путь, что у MoveDistance; `canAcceptCommandNow` для lift: `currentMode == MANUAL || (currentMode == IDLE && isShuttleIdle())` (`.ino:8452-8455`). Lift-команды exempt из in-channel проверки (`isOutOfChannelExemptCommand`, `.ino:8411-8416`) - подъём/опускание разрешены вне канала.
- `run_Cmd`: `CMD_LIFT_UP` (`.ino:3244-3250`): `send_Cmd(); lifter_Up(); status = CMD_STOP; send_Cmd()`; `CMD_LIFT_DOWN` (`.ino:3251-3257`) зеркально.
- MANUAL-путь: ветка `CMD_LIFT_UP || CMD_LIFT_DOWN` (`.ino:1963-1990`): `clearManualRadioHold(); currentOperation = STATE_MANUAL; LOG_INFO "Manual lift start"; send_Cmd(); touchManualSession(); lifter_Up()/lifter_Down()` (`.ino:1972`/`.ino:1976`); по успеху - `status = CMD_MANUAL_MODE`, `STATE_MANUAL`, `LOG_INFO "Manual lift done -> ready"`, `send_Cmd()` (`.ino:1978-1984`); при abort - `LOG_WARN "Manual lift abort"` (`.ino:1988`).
- Физические функции: `lifter_Up` (`.ino:2361-2450`), `lifter_Down` (`.ino:2453-2521`), `lifter_Stop` (`.ino:2524-2536`). Косвенно: `SystemYield`, `shouldAbortLoop`, `preserveManualStopOnAbort`, `blink_Work`, `get_Distance`, `setFault` (`.ino:545-548`), `motor_*` не вызываются.
- Другие вызывающие lifter_Up/lifter_Down (контекст, вне scope): load/unload/long/compact/demo-операции (`.ino:3268, 3276, 3352, 3363, 3378, 5206, 5277, 5339, 5479-5553, 5643-5670, 5859-5930, 6006, 6142, 6243, 6448, 6545, 6696, 7726` и др.); fault-путь MoveDistance (`.ino:4986, 5186`).

#### Admission/preconditions

Пункты 1-5, 7 из admission MoveDistance идентичны (supported/provisioning/error/channel/busy/ACK_OK), с отличиями: lift не требует in-channel (exempt), busy-правило строже (только MANUAL или IDLE+stopped). Аргументов нет (MSG_CMD_SIMPLE достаточно; `MSG_CMD_WITH_ARG` тоже принимается - arg игнорируется, `.ino:2857-2864`).
Физические пред-условия: концевики `DL_UP` (PC13) / `DL_DOWN` (PB4), INPUT_PULLUP (`.ino:355-356`, `.ino:1683-1684`); активное состояние - LOW. Ранние выходы: `lifter_Up` при `!DL_UP && DL_DOWN` (уже вверху, `.ino:2363-2367`), `lifter_Down` при `!DL_DOWN && DL_UP` (`.ino:2455-2458`) - только LOG_DEBUG, без stats/ACK-изменений.

#### Шаги и переходы (lifter_Up; lifter_Down зеркально по знаку)

1. `LOG_INFO "Moove lifter up..."` (`.ino:2368`); CAN-сообщение: `id = 101`, `len = 4` (`.ino:2374-2375`).
2. Ramp: `for (j = 5; j < 50; j += 5)` - 9 шагов (`.ino:2376`), скорость `-j * 1000` (`.ino:2377-2378`), запись в CAN (`.ino:2384`), ожидание 30 ms на шаг: `while (millis() - cnt < 30) { SystemYield(); if (shouldAbortLoop()) { preserveManualStopOnAbort(); return; } }` (`.ino:2385-2393`).
3. Полная скорость `-50000` (`.ino:2397`); запись, если DL_UP не активен (`.ino:2402-2403`).
4. Основной цикл `while (digitalRead(DL_UP))` (`.ino:2404`): `SystemYield()` (`.ino:2406`); abort → preserve + return (`.ino:2407-2411`); timeout: `millis() - cnt > lifterDelay` → `lifter_Stop(); setFault(FAULT_LIFTER_TIMEOUT); LOG_ERROR "Lifter timeout!"; status = CMD_STOP; break` (`.ino:2412-2419`); `delay(10)` (`.ino:2420`); повторная запись кадра (`.ino:2421`, heartbeat); `blink_Work()` (`.ino:2422`); `get_Distance()` (`.ino:2423`).
5. Чтение тока лифтера: дренаж CAN RX, кадры `id == 2405` → `current = buf[4]*256 + buf[5]` (`.ino:2427-2429`); `lifterCurrent` = максимум после k>3 (`.ino:2430-2431`); накопление `summCurrent` при k>3 (`.ino:2433-2434`). Только в lifter_Up; в lifter_Down чтения тока нет.
6. Эпилог: дополнительная запись кадра полной скорости (`.ino:2438`, см. Unknowns); `if (digitalRead(DL_DOWN)) lifterUp = 1` (`.ino:2439-2440`); `lifter_Stop()` (`.ino:2441`); `summCurrent /= k` (`.ino:2442`); перегрузка: `lifterCurrent > 500` → `lifterOverloadCount++`, `lifterCurrent = 250` (`.ino:2443-2447`); `LOG_DEBUG "Summ = %d"` (`.ino:2448`); `liftUpCounter++` (`.ino:2449`).
7. lifter_Down: ramp `+j*1000` (`.ino:2465-2482`), полная `+50000` (`.ino:2486`), цикл `while (digitalRead(DL_DOWN))` (`.ino:2493`), timeout аналогичен (`.ino:2501-2508`); эпилог: `lifterUp = 0` (`.ino:2515-2516`), `lifter_Stop()`, `load = 0`, `lifterCurrent = 0` (`.ino:2517-2519`), `liftDownCounter++` (`.ino:2520`). Дополнительной записи полной скорости после цикла нет (асимметрия с Up).

#### Stop/fault/abort

- CMD_STOP/CMD_STOP_MANUAL: `shouldAbortLoop()` проверяется в каждом 30 ms шаге ramp и каждом обороте основного цикла (`.ino:2388`, `.ino:2407`, `.ino:2477` (down ramp), `.ino:2496`); abort → `preserveManualStopOnAbort(); return` - лифт остаётся в промежуточном положении, `lifter_Stop()` НЕ вызывается в abort-пути (только preserve+return).
- Fault во время исполнения: любой active fault → `shouldAbortLoop()` → тот же выход. `FAULT_LIFTER_TIMEOUT` latch-ится самой функцией по `lifterDelay` (`.ino:2415`/`.ino:2504`, `SP.h:179`, бит 1<<9), после неё `status = CMD_STOP`, `break` → эпилог выполняется (включая возможную установку `lifterUp = 1`, см. Unknowns).
- Отказ сенсоров: концевики не опрашиваются на отказ; ToF-сенсоры опрашиваются (`get_Distance` в цикле) и способны latch-ить ToF-fault → abort через `shouldAbortLoop`.
- SystemYield вызывается в каждом ожидании (ramp и основной цикл); `delay(10)` в основном цикле блокирующий (`.ino:2420`).
- Переход в ERROR: fault, выставленный в операции, перехватывается в начале следующего прохода `loop()`: `if (isErrorActive()) currentMode = CoreOpMode::ERROR` (`.ino:1819-1820`); ERROR-case принудительно останавливает движение `motor_Force_Stop()` если не stationary (`.ino:2050-2056`) и обрабатывает только CMD_SYSTEM_RESET/CMD_RESET_ERROR/CMD_SAVE_EEPROM (`.ino:2060-2074`).

#### Observable outcomes

- Успех Up: `lifterUp = 1` (если DL_DOWN не активен), `liftUpCounter++`, при `lifterCurrent > 500` - `lifterOverloadCount++`; статус в run_Cmd → `CMD_STOP` (`.ino:3248`), далее IDLE (`.ino:1902-1907`). Успех Down: `lifterUp = 0`, `load = 0`, `lifterCurrent = 0`, `liftDownCounter++`.
- Telemetry state при приёмке: `STATE_LIFT_UP = 15` / `STATE_LIFT_DOWN = 16` (`mapCmdToOperation` `.ino:8255-8258`, `SP.h:162-163`).
- Fault: `FAULT_LIFTER_TIMEOUT` (`.ino:2415`/`.ino:2504`) → ERROR на следующем проходе loop; `LOG_ERROR "Lifter timeout!"`.
- Логи: `LOG_INFO "Moove lifter up/down..."` (`.ino:2368`/`.ino:2460`), `LOG_DEBUG "Lifter is up/down..."` при раннем выходе (`.ino:2365`/`.ino:2457`), `LOG_INFO "Stop lifter..."` (`.ino:2526`).
- Ручная сессия: по успеху `status = CMD_MANUAL_MODE`, `STATE_MANUAL`, `LOG_INFO "Manual lift done -> ready"` (`.ino:1978-1984`).
- `lifterUp` влияет на последующее движение: мягкий разгон/торможение в `motor_Speed` (`.ino:2123-2126`, `.ino:2176-2177`), 3000 ms escape в moove_Distance (`.ino:4977`/`.ino:5177`), спец-кейсы `motor_Stop` для unload (`.ino:2288-2294`, `.ino:2305-2316`).

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| 9 шагов ramp (j=5..45, шаг 5) | `.ino:2376`, `.ino:2465` | configured | разгон лифтера до полной скорости |
| 30 ms на шаг ramp | `.ino:2385`, `.ino:2473` | configured | интервал между ступенями скорости |
| 3800 ms (`lifterDelay`) | `.ino:1646`, `.ino:2412`, `.ino:2501` | configured | максимум времени хода до FAULT_LIFTER_TIMEOUT; в EEPROM не сохраняется, константа |
| 10 ms | `.ino:2420`, `.ino:2509` | configured | пауза основного цикла между heartbeat-записями CAN |
| k > 3 | `.ino:2430-2434` | configured | прогрев выборки тока: максимум/сумма только после 3 кадров |
| порог перегрузки 500 | `.ino:2443` | configured | единицы raw CAN 2405; физический смысл из source не устанавливается |

#### Resource effects

- CAN ID 101 (extended, len 4): скорость лифтера, big-endian int32 (`.ino:2374-2383`); значения: ramp ±j*1000, полная ±50000, стоп 0 (`lifter_Stop` `.ino:2524-2536`).
- CAN ID 2405 (RX): ток лифтера, `buf[4]*256+buf[5]` (`.ino:2427-2429`) - только при подъёме.
- GPIO: DL_UP PC13, DL_DOWN PB4 (INPUT_PULLUP, `.ino:355-356`, `.ino:1683-1684`); CHANNEL не используется.
- I2C: ToF через `get_Distance` в каждом обороте цикла (`.ino:2423`/`.ino:2512`).
- UART: `send_Cmd()` до/после в run_Cmd (`.ino:3246-3249`); логирование.
- Delays: `delay(10)` в основном цикле (`.ino:2420`, `.ino:2509`); SystemYield в каждом ожидании.

#### Профильные варианты 800/1000/1200

В lifter_Up/lifter_Down профильных ветвлений нет. Config `lifter_Speed = 3700` (`.ino:1645`, сохраняется/читается EEPROM `.ino:7594`/`.ino:7628`) в логике лифтера НЕ используется (скорости захардкожены ±50000 и ramp 5..45) - мёртвый config.

#### Unknowns

- Физические единицы тока CAN 2405 и порога 500; единицы скорости лифтера (±50000).
- Поведение `summCurrent /= k` (`.ino:2442`) при k==0: возможно, если оба концевика одновременно активны (ранний выход не срабатывает, т.к. условие требует ровно одного) и цикл `while (DL_UP)` не executes; последствия деления на 0 на целевой платформе из source не устанавливаются.
- После timeout эпилог может выставить `lifterUp = 1` (`.ino:2439-2440`), если DL_DOWN не активен, хотя платформа в промежуточном положении - факт зафиксирован, замысел не устанавливается.
- Назначение дополнительной записи кадра полной скорости в эпилоге Up (`.ino:2438`).
- Почему abort-путь не вызывает `lifter_Stop()` (лифт продолжает получать последний кадр скорости до watchdog/CAN-таймаута привода - поведение привода из source не устанавливается).

#### Disposition proposals (предложения, не решения)

- V1-факт: концевики как единственный источник завершения хода + timeout 3800 ms → FAULT_LIFTER_TIMEOUT (`.ino:2404-2419`). Предложение: preserve концепцию.
- V1-факт: abort не вызывает lifter_Stop (`.ino:2388-2391`, `.ino:2407-2411`). Предложение: change - явная остановка привода при abort.
- V1-факт: `lifterUp` может стать 1 после timeout (`.ino:2439-2440`). Предложение: change - не менять флаг состояния при fault.
- V1-факт: выборка тока только при подъёме, overload-порог 500 → счётчик и clamp 250 (`.ino:2425-2447`). Предложение: preserve как оценку массы/перегрузки; unknown - единицы.
- V1-факт: `lifter_Speed` config не используется (`.ino:1645` vs `.ino:2397`). Предложение: exclude (мёртвый config) либо change - реально применить.
- V1-факт: lift разрешён вне канала (exempt, `.ino:8411-8416`). Предложение: preserve (продуктное решение).
- V1-факт: деление `summCurrent /= k` при возможном k==0 (`.ino:2442`). Предложение: change - guard.
- V1-факт: асимметрия эпилогов Up/Down (доп. запись кадра только в Up, `.ino:2438`). Предложение: exclude как случайный артефакт - требует подтверждения замысла (unknown).

---

### 3. Примитивы движения

#### motor_Speed(int spd) (CAN ID 100) - `.ino:2088-2236`

- No-op, если `motorReverse == 2` (движение не активно) или прошло < 50 ms с последней записи (`countMoove`, `.ino:2090-2092`); дренаж CAN RX (`.ino:2093`).
- Rate-limit: при `spd >= 10 && spd - oldSpeed >= 10` → `spd = oldSpeed + 10` (`.ino:2099-2100`); кап 100 (`.ino:2101-2102`).
- Разгон (`spd > oldSpeed`, `.ino:2105-2152`): `steps = (spd - oldSpeed)/2`; на шаге: `blink_Work()`, `get_Distance()`, интерполированная скорость `minSpeed + oldSpeed*maxSpeed/100 + (spd-oldSpeed)*i*maxSpeed/(steps*100)` (`.ino:2111`), знак по `motorReverse ^ inverse`, `*1000`, CAN id=100, `set_Position()`; ожидание `accel` ms: при `lifterUp && i in (2,40]` → `80 - i*15/10`, иначе 35 (`.ino:2123-2127`); внутри ожидания `SystemYield()`, `blink_Work()`, `get_Distance()`, `shouldAbortLoop()` → preserve + `motor_Stop()` + return (`.ino:2128-2137`). Финальная точная запись скорости, `oldSpeed = spd` (`.ino:2139-2152`).
- Торможение (`spd < oldSpeed`, `.ino:2154-2203`): `steps = (oldSpeed - spd)/2`; удвоение темпа при близком препятствии: `distance[0] <= 400 || distance[1] <= 400 → steps /= 2` (`.ino:2157-2158`); ожидание: при `lifterUp` → `10 + i*20/steps`, иначе `accel` остаётся 18 (инициализация `.ino:2096`); внутри ожидания дополнительно выход при `motorReverse == 2` (`.ino:2188-2189`).
- Равенство (`spd == oldSpeed`, `.ino:2204-2221`): повторная запись текущей скорости. Вне диапазона 0..100 (`.ino:2223-2234`): запись 0, `oldSpeed = 0`. В конце `set_Position()` (`.ino:2235`).
- Глобальные настройки: `maxSpeed = 96`, `minSpeed = 3` (default, `.ino:591-592`, EEPROM). Формула скорости: физическая скорость = `minSpeed + spd*maxSpeed/100` (в единицах привода, *1000 в кадре).
- Вызывающие: moove_Distance_F/R (тики скорости), moove_Forward/Reverse, moove_Right/Left, stop_Before_Pallete_*, unload-пути.

#### motor_Start_Forward / motor_Start_Reverse (направление, "motor_Reverse") - `.ino:2239-2247`, `.ino:2250-2258`

- ToF freshness gate: `ensureChannelTofReadyForMotion(forward, "start_f"/"start_r")` (`.ino:2241`, `.ino:2252`; определение `.ino:3536-3553`): slot 1 (forward) / slot 0 (reverse) → `tofSensorForDistanceSlot` (`.ino:3483-3503`, inverse меняет сенсоры местами) → `tofHealth[sensor].measurementReady(now)` (TofHealthMonitor: outputValid + полное окно 16 выборок, `kFreshFrameTimeoutMs = 300 ms`, `TofHealthMonitor.h:41-52`, `.cpp:203-211`); при провале - `latchTofMeasurementFault` (`.ino:3505-3534`, fault TOF_CH_F/TOF_CH_R) и возврат БЕЗ установки флагов. Bypass при `sensorOff` (`.ino:3538-3541`).
- Успех: `motorStart = 1; motorReverse = 0/1`.
- Вызывающие: moove_Distance_F (`.ino:4834`), moove_Distance_R (`.ino:5035`), moove_Forward (`.ino:5200`), moove_Reverse (`.ino:5271`), moove_Left/Right (`.ino:6633`/`.ino:6564`), unload-пути.

#### motor_Stop() - `.ino:2261-2334` (управляемый останов)

- No-op при `motorReverse == 2` (`.ino:2263-2264`); `LOG_INFO "Motor stop, speed = %d"` (`.ino:2266`).
- Ramp вниз от `oldSpeed`: `maxi = oldSpeed/2`, цикл `i = maxi..1` с убывающей скоростью (`.ino:2271-2294`); задержки: `delay(10)` в unload+lifted-кейсе (`status == CMD_LONG_UNLOAD || CMD_UNLOAD && lifterUp && distance[3]+100 < distance[1]`, `.ino:2288-2289`), иначе `maxi > 10 → delay(10 + i*20/maxi)`, иначе `delay(50)` (`.ino:2290-2294`).
- Нулевой кадр + повторные нулевые кадры: 10 раз по 100 ms в unload+lifted-кейсе, иначе 1 раз 100 ms (`.ino:2296-2327`).
- Сброс: `oldSpeed=0, motorStart=0, motorReverse=2, mooveCount=0, oldPosition=currentPosition` (`.ino:2328-2333`).
- Блокирующие `delay()` без SystemYield (кроме возможного входа через `blink_Work`); вызывается из всех операций движения, из SystemYield при CMD_STOP (`.ino:6973`), из motor_Speed при abort.

#### motor_Force_Stop() - `.ino:2337-2355` (экстренный останов)

- Один нулевой кадр CAN ID 100 без ramp и логов; сброс тех же флагов (`.ino:2351-2354`).
- Вызывающие: ERROR-case loop при не-stationary (`.ino:2053-2056`), `enforceActiveMotionTofSafety` (`.ino:3570`), handlePendingCrash (bumper, `.ino:7798` и далее - cross-cutting).

#### moove_Forward() - `.ino:5197-5265` (движение вперёд до конца канала)

- `motor_Start_Forward()` (`.ino:5200`); `detect_Pallete()` (`.ino:5201`); при `lifterUp` → `stop_Before_Pallete_F()` (`.ino:4292` и далее) + `lifter_Down()` + return (`.ino:5202-5208`) - с грузом едет до паллета и опускает платформу.
- Цикл `while (moove)` БЕЗ общего deadline: `SystemYield`, abort → preserve+motor_Stop+return, `blink_Work`, `get_Distance`, тик 50 ms (`.ino:5224`): `set_Position`, `detect_Pallete`, clamp `currentPosition >= 0`; профиль скорости: `distance[1] >= 1500 → 100`; `> 90+chnlOffset → distance[1]/20` с ограничениями (не выше oldSpeed при oldSpeed>5, пол 5, кап 80, не выше oldSpeed при oldSpeed>50) (`.ino:5231-5245`); `<= 90+chnlOffset`: для LOAD/LONG_LOAD и distance>80 → скорость 5, иначе стоп: `speed=0, moove=0, currentPosition=60`, LOG_INFO "End of channel" (`.ino:5246-5256`); `motor_Speed(speed)`; повторный abort-check (`.ino:5259-5260`).
- Выходы: конец канала (distance[1] <= 90+chnlOffset), abort (stop/ошибка). Выхода по obstacle внутри самого цикла нет - только через ToF force-stop (fault) или bumper (cross-cutting).
- После цикла `motor_Stop()` (`.ino:5264`).
- Вызывающие (в scope): run_Cmd для CMD_MOVE_RIGHT_MAN (`.ino:3240`), CMD_UNLOAD (`.ino:3277`, `.ino:3280`), CMD_CALIBRATE (`.ino:3304`), CMD_COUNT_PALLETS (`.ino:3321`), CMD_COMPACT_F/R (`.ino:3336`, `.ino:3346`), CMD_LONG_LOAD (`.ino:3355`), CMD_LONG_UNLOAD (`.ino:3364`, `.ino:3370`), CMD_LONG_UNLOAD_QTY (`.ino:3379`, `.ino:3387`), CMD_HOME (`.ino:3393`); плюс операции вне scope (single_Load, long_Load, demo и др.).

#### moove_Reverse() - `.ino:5268-5326` (движение назад до конца канала)

- Зеркален moove_Forward: slot `distance[0]`, `motor_Start_Reverse()` (`.ino:5271`), lifted-ветка через `stop_Before_Pallete_R()` (`.ino:4518` и далее, `.ino:5273-5278`).
- Тик: `millis() - count > timingBudget + 5` (`.ino:5295`), `timingBudget = 40` default (`.ino:1649`, сохраняется/читается EEPROM `.ino:7595`/`.ino:7629`) - т.е. 45 ms против 50 ms у Forward; происхождение "+5" из source не устанавливается.
- Конец канала: `channelLength = currentPosition + shuttleLength + distance[0] - 30`, `endOfChannel = 1` (`.ino:5308-5317`); профиль скорости как у Forward без капа 80 и без LOAD-исключения (`.ino:5298-5307`).
- Выходы: конец канала, abort. После цикла `motor_Stop()` (`.ino:5325`).
- Вызывающие (в scope): run_Cmd для CMD_MOVE_LEFT_MAN (`.ino:3233`); вне scope: demo_Mode (`.ino:6697`), calibrate (`.ino:7363` через moove_Distance_R(2000) - фактически дистанционный вариант).

#### lifter_Stop() - `.ino:2524-2536`

- Один нулевой кадр CAN ID 101; вызывается из lifter_Up/Down (норма и timeout).

---

### 4. Ручные непрерывные движения (контекст shared primitives)

#### CMD_MOVE_RIGHT_MAN = 0x10 / CMD_MOVE_LEFT_MAN = 0x11 (`SP.h:97-98`)

- V1 manual session, не root operation: исполняются в case MANUAL через `moove_Right()` (`.ino:6559-6624`, фактически REVERSE-hold - `motor_Start_Reverse`, лог "Manual reverse hold") и `moove_Left()` (`.ino:6627-6690`, фактически FORWARD-hold - `motor_Start_Forward`, лог "Manual forward hold"); имена функций инвертированы относительно направления.
- Общие с MoveDistance примитивы: `motor_Start_*`, `motor_Speed`, `motor_Stop`, `get_Distance`, `set_Position`, `SystemYield`, `shouldAbortLoop`. Отличия: ramp `manualCount` 6→60 (+3 каждые 50 ms, `.ino:6590-6592`/`.ino:6656-6658`), ограничение скорости по `distance[slot]/25` (`.ino:6600-6604`), bypass при `sensorOff` (`.ino:6594`/`.ino:6660`), конец канала → `status = CMD_MANUAL_MODE` + `WARN_END_OF_CHANNEL` 3000 ms (`.ino:6606-6614`), выход при `status != CMD_MOVE_X_MAN` (`.ino:6580-6585`) - управление удержанием кнопки; radio-hold watchdog 3000 ms в SystemYield (`.ino:7041-7051`, `kManualRadioHoldWatchdogMs` `.ino:395`).
- В run_Cmd тоже есть ветки MOVE_LEFT_MAN/RIGHT_MAN (`.ino:3230-3243`) - путь AUTO_EXEC (например, при CMD_UNLOAD `status = CMD_MOVE_RIGHT_MAN` выставляется программно, `.ino:3279`).
- Admission: continuous-manual команды требуют MANUAL-режим для radio или idle для display (`canAcceptCommandNow` `.ino:8437-8445`); manual session timeout 60000 ms → WARN_MANUAL_TIMEOUT и выход в IDLE (`.ino:2012-2021`, `kManualSessionIdleTimeoutMs` `.ino:394`).

---

### 5. Distance-обслуживание в loop() как источник distance[]

- Один `get_Distance()` attempt за foreground-цикл: `loop()` вызывает его на каждой итерации (`.ino:1817`), после `SystemYield()` (`.ino:1808`) и `handlePendingCrash()` (`.ino:1809`), до switch по currentMode; дополнительных вызовов из SystemYield нет.
- Внутренние ограничения одного attempt: межопросный интервал >= 8 ms (`countSensor`, `.ino:3861-3862`); BMS TX quiet guard 5 ms (`kBmsTofQuietGuardMs` `.ino:403`, проверка `.ino:3864-3869`); при неготовности I2C-шины (`serviceTofBusMonitor`) - только `enforceActiveMotionTofSafety` и return (`.ino:3871-3875`).
- Round-robin: один сенсор за вызов, `sensorIndex = (sensorIndex + 1) % 4` (`.ino:3880-3881`), т.е. полный круг 4 сенсора = 4 цикла loop/вызова.
- Запись в `distance[8]` (`.ino:594`) через `updateTofDistance` (`.ino:3796-3809`): sensor 0 → distance[0] (канал R), 1 → distance[1] (канал F), 2/3 → паллетные distance[2]/[3]; при `inverse` пары меняются местами. Slot→sensor mapping: `tofSensorForDistanceSlot` (`.ino:3483-3503`).
- Дополнительно `get_Distance()` вызывается внутри операций: в тиках moove_Distance_F/R (`.ino:4805`, `.ino:4835`, `.ino:4848`), moove_Forward/Reverse (`.ino:5209`, `.ino:5223`), motor_Speed на каждом шаге ramp (`.ino:2110`, `.ino:2132`, `.ino:2164`, `.ino:2181`), motor_Stop ramp (`.ino:2276`), lifter_Up/Down циклы (`.ino:2423`, `.ino:2512`), moove_Right/Left (`.ino:6566`, `.ino:6587`, `.ino:6632`, `.ino:6653`). Поэтому во время движения distance[] обновляется чаще, чем 1 раз за loop.
- Freshness/staleness: `checkTofStale(now)` на каждом входе/выходе get_Distance (`.ino:3860`, `.ino:3891`, `.ino:3916`; определение `.ino:3697`); при движении `enforceActiveMotionTofSafety` (`.ino:3917`, определение `.ino:3555-3571`) - force-stop при отсутствии валидного выхода сенсора направления.
- I2C-ошибки: `recordTofFailure` + повторный `checkTofStale` + safety (`.ino:3888-3893`); успех: `noteMeasurement` → фильтр → `updateTofDistance` (`.ino:3896-3914`).

---

### Сквозные замечания (мёртвый код, ordering-баги, асимметрии)

1. Ordering-баг (F): клампизация `minSpeed > maxSpeed` до dist-капов (`.ino:4822-4829`) - после капа maxSpeed может стать меньше minSpeed; в R порядок корректный (`.ino:5021-5034`). Зафиксировано, не интерпретируется.
2. Асимметрия F/R: F клампит `maxSpeed < 2`, R клампит `maxSpeed < 5` (`.ino:4820` vs `.ino:5031`); F принимает `diff != 0` после clamp (`.ino:4935-4940`), R требует `diff > 0` (`.ino:5137`).
3. Мёртвый config `lifter_Speed` (`.ino:1645`, EEPROM `.ino:7594`/`.ino:7628`) - не используется в lifter_Up/Down.
4. `accel = 18` (`.ino:2096`) используется только в ветке торможения без lifterUp; в ветке разгона всегда перезаписывается (`.ino:2123-2127`).
5. lifter_Up эпилог: дополнительная запись кадра полной скорости после выхода из цикла (`.ino:2438`) и возможное деление на k==0 (`.ino:2442`); lifter_Down симметричных записей не имеет.
6. После fault-завершения операции AUTO_EXEC выставляет `currentOperation = STATE_IDLE, currentMode = IDLE` (`.ino:1902-1907`) в том же проходе; ERROR выставляется только на следующей итерации loop (`.ino:1819-1820`) - один цикл телеметрия может показывать IDLE при уже latch-нутом fault.
7. Аборт в lifter_Up/Down не вызывает lifter_Stop (`.ino:2388-2391`, `.ino:2407-2411`).
8. `mooveDistance` - `uint16_t` при int32-аргументе протокола (`.ino:596`, `.ino:2861`, `SP.h:369`) - truncation без валидации.
9. В IDLE-приёмке lift-команды exempt из in-channel проверки (`.ino:8411-8416`) - lift исполняется вне канала по дизайну.
10. `fifoLifo_Inverse` (`.ino:4037-4049`) и CFG_REVERSE_MODE (`.ino:2997-3004`) пересчитывают `currentPosition` с hardcoded 800 независимо от shuttleLength - зафиксировано как факт.

---

## Группа: LoadPallet / UnloadPallet

Источник: локальное зеркало `C:/Projects/Shuttle/ShuttleController`. Evidence SHA: `708d090980155d4a8d4644f7bcf87c383e81cd1d` (проверено: HEAD `4a226e5` отличается только `docs/Controller-Nonblocking-Refactoring-Plan.md`, diff = 1914 строк добавлено, исходники идентичны). Номера строк - по working tree. Файлы: `Cntrl_V2/Cntrl_V2.ino` (9063 строки), `Cntrl_V2/ShuttleProtocol.h` (612 строк).

Уточнение к тиkету: в реальном файле `unload_Pallete()` занимает L5329-L5659, `load_Pallete()` - L5662-L5939, `single_Load()` - L5942-L6000 (порядок и границы отличаются от подсказок тикета; все anchors ниже выверены по содержимому).

Общие коды: `CMD_LOAD = 0x20`, `CMD_UNLOAD = 0x21` (`Cntrl_V2/ShuttleProtocol.h:L106-L107`); `STATE_LOAD = 2`, `STATE_UNLOAD = 3` (`Cntrl_V2/ShuttleProtocol.h:L149-L150`); ACK-коды `Cntrl_V2/ShuttleProtocol.h:L136-L143`; faults/warnings `Cntrl_V2/ShuttleProtocol.h:L168-L197`.

Общий admission-путь (одинаков для обеих операций, факты): приём кадра `pollSerial` -> `processPacket` (`Cntrl_V2/Cntrl_V2.ino:L2713`); проверки по порядку: фильтр targetID (`Cntrl_V2/Cntrl_V2.ino:L2715-L2730`); `isSupportedCommand` (LOAD: `Cntrl_V2/Cntrl_V2.ino:L8369`, UNLOAD: `Cntrl_V2/Cntrl_V2.ino:L8370`; при отказе ACK_REJECTED, `Cntrl_V2/Cntrl_V2.ino:L2782-L2786`); provisioning - LOAD/UNLOAD не входят в `isUnprovisionedCommandAllowed` (`Cntrl_V2/Cntrl_V2.ino:L8406-L8409`), при непривязанном ID ACK_BAD_ENVIRONMENT (`Cntrl_V2/Cntrl_V2.ino:L2819-L2828`); active error -> ACK_ERROR_STATE, т.к. LOAD/UNLOAD не override (`Cntrl_V2/Cntrl_V2.ino:L2832-L2841`, `Cntrl_V2/Cntrl_V2.ino:L8418-L8423`); `inChannel = digitalRead(CHANNEL)` (`Cntrl_V2/Cntrl_V2.ino:L2830`), LOAD/UNLOAD не exempt (`Cntrl_V2/Cntrl_V2.ino:L8411-L8416`) -> WARN_NOT_IN_CHANNEL + ACK_BAD_ENVIRONMENT (`Cntrl_V2/Cntrl_V2.ino:L2843-L2853`); `canAcceptCommandNow` (`Cntrl_V2/Cntrl_V2.ino:L8430-L8463`): обе команды попадают в ветвь `return isShuttleIdle()` (`Cntrl_V2/Cntrl_V2.ino:L8462`, `isShuttleIdle` = status==0||CMD_STOP, `Cntrl_V2/Cntrl_V2.ino:L8425-L8428`), иначе ACK_BUSY (`Cntrl_V2/Cntrl_V2.ino:L2874-L2883`). При приёме: ACK_OK с `predictTelemetryStateForAcceptedCommand` (`Cntrl_V2/Cntrl_V2.ino:L2855-L2873`, `Cntrl_V2/Cntrl_V2.ino:L8271-L8291`, `mapCmdToOperation` LOAD/UNLOAD: `Cntrl_V2/Cntrl_V2.ino:L8233-L8236`). Аргументы не читаются: LOAD/UNLOAD не входят в обработку `MSG_CMD_WITH_ARG` (`Cntrl_V2/Cntrl_V2.ino:L2857-L2864`). Присвоение `status` происходит повторно в `SystemYield` с повторной проверкой `canAcceptCommandNow` (`Cntrl_V2/Cntrl_V2.ino:L6975-L7039`); STOP имеет приоритет (`Cntrl_V2/Cntrl_V2.ino:L6962-L6974`). Повторная проверка inChannel с тройным опросом при входе из IDLE (`Cntrl_V2/Cntrl_V2.ino:L1826-L1840`); при входе в операцию `lastPalletePosition = 0` (`Cntrl_V2/Cntrl_V2.ino:L1845`), `currentOperation = mapCmdToOperation(status)` (`Cntrl_V2/Cntrl_V2.ino:L1843`), режим `CoreOpMode::AUTO_EXEC` (`Cntrl_V2/Cntrl_V2.ino:L1870`). Протокольный ACK отправляется `sendCommandAck`/`sendAck`/`sendTelemAck` (`Cntrl_V2/Cntrl_V2.ino:L2664-L2711`); `send_Cmd()` - это телеметрия в дисплейный порт `sendTelemetryPacket(&SerialDisplay)` (`Cntrl_V2/Cntrl_V2.ino:L3400-L3403`), вызывается на границах операции.

---

### LoadPallet (CMD_LOAD = 0x20)

**Entry points и call sites**

- Объявление команды: `Cntrl_V2/ShuttleProtocol.h:L106`. Supported-список: `Cntrl_V2/Cntrl_V2.ino:L8369`. Маппинг в STATE_LOAD: `Cntrl_V2/Cntrl_V2.ino:L8233-L8234`.
- Авто-путь: `loop()` IDLE -> AUTO_EXEC (`Cntrl_V2/Cntrl_V2.ino:L1870`) -> `run_Cmd()` (`Cntrl_V2/Cntrl_V2.ino:L1898-L1909`) -> ветвь CMD_LOAD `Cntrl_V2/Cntrl_V2.ino:L3265-L3272`: `send_Cmd()` -> `lifter_Down()` -> `single_Load()` -> `status = CMD_STOP` -> `send_Cmd()`. После `run_Cmd` обёртка AUTO_EXEC: `if (status != CMD_STOP) status = 0; currentOperation = STATE_IDLE; currentMode = IDLE` (`Cntrl_V2/Cntrl_V2.ino:L1902-L1907`).
- Ручной путь: в `CoreOpMode::MANUAL` ветви `status == CMD_LOAD` (`Cntrl_V2/Cntrl_V2.ino:L1991-L1995`) и `status == CMD_UNLOAD` (`Cntrl_V2/Cntrl_V2.ino:L1996-L2000`) выставляют `currentOperation` и вызывают `run_Cmd()`. Достижимость: acceptance требует `isShuttleIdle()`, т.е. status==CMD_STOP/0 в момент приёма; в MANUAL это окно появляется, например, после abort ручного шага, когда `SystemYield` внутри той же итерации MANUAL-ветви принимает новую команду до перехода в IDLE (переход в IDLE по `status == CMD_STOP` - `Cntrl_V2/Cntrl_V2.ino:L2040-L2047` - происходит в конце итерации). (Суждение: окно узкое, но ветвь не мёртвая.)
- `single_Load()` (`Cntrl_V2/Cntrl_V2.ino:L5942-L6000`): `moove_Forward()` (`Cntrl_V2/Cntrl_V2.ino:L5197-L5265`) -> `get_Distance()` (`Cntrl_V2/Cntrl_V2.ino:L3857-L3919`) -> `detect_Pallete()` (`Cntrl_V2/Cntrl_V2.ino:L3406-L3464`) -> ветвление: `load_Pallete()` / warning / обратный поиск; завершается безусловным `moove_Forward()` (`Cntrl_V2/Cntrl_V2.ino:L5999`).
- `load_Pallete()` (`Cntrl_V2/Cntrl_V2.ino:L5662-L5939`) вызывает: `lifter_Down` (`Cntrl_V2/Cntrl_V2.ino:L2453-L2521`), `get_Distance`, `detect_Pallete`, `moove_Before_Pallete_F` (`Cntrl_V2/Cntrl_V2.ino:L4410-L4515`), `motor_Start_Forward` (`Cntrl_V2/Cntrl_V2.ino:L2239-L2247`)/`motor_Speed` (`Cntrl_V2/Cntrl_V2.ino:L2088-L2236`)/`motor_Stop` (`Cntrl_V2/Cntrl_V2.ino:L2261-L2334`), `moove_Distance_F` (`Cntrl_V2/Cntrl_V2.ino:L4803-L4994`), `moove_Distance_R` (`Cntrl_V2/Cntrl_V2.ino:L5004-L5194`), `lifter_Up` (`Cntrl_V2/Cntrl_V2.ino:L2361-L2450`), `stop_Before_Pallete_R` (`Cntrl_V2/Cntrl_V2.ino:L4518-L4637`, внутри `moove_Before_Pallete_R` `Cntrl_V2/Cntrl_V2.ino:L4640-L4793`).
- Косвенные вызовы `load_Pallete` вне CMD_LOAD (контекст): `pallete_Compacting_F` (`Cntrl_V2/Cntrl_V2.ino:L6171`), `long_Load` (`Cntrl_V2/Cntrl_V2.ino:L6313`), demo (`Cntrl_V2/Cntrl_V2.ino:L6746`), evacuate-контекст (`Cntrl_V2/Cntrl_V2.ino:L6746` - demo; другие call sites по grep: 6171, 6313, 6746).

**Admission/preconditions**

- Полный протокольный admission - см. общий блок выше (source/target, supported, provisioning, error-state, inChannel, isShuttleIdle; ACK_REJECTED/BAD_ENVIRONMENT/ERROR_STATE/BUSY).
- Специфических проверок аргументов нет (команда без аргумента).
- Внутренние preconditions в `load_Pallete`: проверка «канал не забит» `if (lastPalletePosition && lastPalletePosition < shuttleLength * 2)` -> WARN_CHANNEL_FULL + CMD_STOP (`Cntrl_V2/Cntrl_V2.ino:L5671-L5677`). Факт: при свежем CMD_LOAD `lastPalletePosition` обнуляется в момент приёма команды (`Cntrl_V2/Cntrl_V2.ino:L1845`), поэтому для одиночной загрузки проверка всегда пропускается; она срабатывает только при повторных вызовах `load_Pallete` в compact/long/demo.
- Проверка стартовой позиции: `!((detectPalleteF1 || detectPalleteF2 || detectPalleteR1 || detectPalleteR2) && distance[1] < 450 + chnlOffset && distance[3] > 400)` определяет, нужна ли фаза подхода (`Cntrl_V2/Cntrl_V2.ino:L5682-L5685`).

**Шаги и переходы (normal path)**

1. `run_Cmd`: `send_Cmd()` (телеметрия), `lifter_Down()` (`Cntrl_V2/Cntrl_V2.ino:L3267-L3268`).
2. `single_Load`: `moove_Forward()` - движение к началу канала (`Cntrl_V2/Cntrl_V2.ino:L5944`); при `lifterUp` `moove_Forward` вместо движения к концу канала выполняет `stop_Before_Pallete_F()` + `lifter_Down()` и возвращает (`Cntrl_V2/Cntrl_V2.ino:L5202-L5208`); останов у начала канала по `distance[1] <= 90 + chnlOffset`, при CMD_LOAD в зоне `distance[1] > 80` - creep скоростью 5 вместо остановки (`Cntrl_V2/Cntrl_V2.ino:L5246-L5256`), `currentPosition = 60` при останове (`Cntrl_V2/Cntrl_V2.ino:L5254`). `shouldAbortLoop()` после `moove_Forward` -> return (`Cntrl_V2/Cntrl_V2.ino:L5945-L5946`).
3. Опрос датчиков и выбор ветви (`Cntrl_V2/Cntrl_V2.ino:L5947-L5957`): (F1&&F2 && shuttleLength != 800) или (все 4 датчика) -> `load_Pallete()`; (F1&&F2 && shuttleLength == 800) -> WARN_PALLET_SIZE_ERROR (без остановки, без return); иначе - обратный поиск.
4. Обратный поиск (`Cntrl_V2/Cntrl_V2.ino:L5959-L5980`): `motor_Start_Reverse()`, `motor_Speed(10)`, цикл `while (!(F1&&F2) && distance[1] < 400 + chnlOffset)` с `SystemYield()` и 50 мс pace (`set_Position` + `motor_Speed(10)`); выходы: детект F-пары, `distance[1] >= 400 + chnlOffset`, abort (`motor_Stop`, status=CMD_STOP, return).
5. Повторное ветвление после поиска (`Cntrl_V2/Cntrl_V2.ino:L5981-L5997`): успех -> `diffPallete = 10` + `load_Pallete()`; F-пара при 800 -> WARN_PALLET_SIZE_ERROR; ничего -> WARN_PALLET_NOT_FOUND + лог "Single load fail...".
6. `load_Pallete`: `lifter_Down`; проверка канала (см. Admission); `startDiff = 20` если `distance[1] < 90 + chnlOffset` (`Cntrl_V2/Cntrl_V2.ino:L5678-L5680`); фаза подхода: `moove_Before_Pallete_F()` при `distance[3] > 750` (`Cntrl_V2/Cntrl_V2.ino:L5687-L5690`); старт под паллету при `distance[1] > 150`: `motor_Start_Forward`, скорость oldSpeed или 20 (`Cntrl_V2/Cntrl_V2.ino:L5697-L5705`).
7. Цикл поиска досок вперёд (`Cntrl_V2/Cntrl_V2.ino:L5708-L5812`): `SystemYield()` + `shouldAbortLoop()` в голове; `blink_Work()`, `get_Distance()`, `detect_Pallete()`; при (F1&&F2 && !frontBoard): `set_Position`, фиксация `currentPalletePosition`, фиксированное перемещение `moove_Distance_F(dst, oldSpeed, 10)`, dst = 600 (670 для 1200) (`Cntrl_V2/Cntrl_V2.ino:L5720-L5731`), `frontBoard = 1`; при (F1&&F2 && frontBoard): board-delay цикл из `maxbb` шагов по 100 мс (`Cntrl_V2/Cntrl_V2.ino:L5745-L5768`), внутри каждого `SystemYield`+`blink_Work`+abort-проверка, `palleteLenght = abs(currentPalletePosition - currentPosition)`, ранний выход при (R1&&R2); затем `moove = 0`, `motor_Stop`, проверка `frontBoard && palleteLenght >= shuttleLength - 20` -> WARN_PALLET_SIZE_ERROR + `moove_Forward()` + CMD_STOP + return (`Cntrl_V2/Cntrl_V2.ino:L5769-L5784`); 50 мс pace-ветвь: скорость `distance[1]/23` (clamp 5..oldSpeed), выход у конца канала `distance[1] < 80 && (R1||R2) && (F1||F2)` -> `startDiff = 20`, стоп (`Cntrl_V2/Cntrl_V2.ino:L5796-L5801`), и timeout `millis() - cnt > 2000000 / maxSpeed || distance[1] < 80` -> WARN_OBSTACLE_AHEAD + return (`Cntrl_V2/Cntrl_V2.ino:L5802-L5809`).
8. Альтернативные ветви стартового состояния (`Cntrl_V2/Cntrl_V2.ino:L5814-L5854`): (любой датчик && distance[1] < 300+chnlOffset && distance[2] <= interPalleteDistance+600) -> WARN_OBSTACLE_AHEAD + CMD_STOP + return; только R-пара -> цикл одноразового отката `moove_Distance_R(10,10,10)` до `distance[1] < 70 || R-пара` (фактически одна итерация, т.к. R-пара уже true), затем при `distance[3] < 500 && distance[3] < distance[1]` -> WARN_OBSTACLE_AHEAD + CMD_STOP; F-пара -> `frontBoard = 0` (паллета уже сверху).
9. Подъём: `lifter_Up()` (`Cntrl_V2/Cntrl_V2.ino:L5859`).
10. Recapture при `!R1 || !R2` (`Cntrl_V2/Cntrl_V2.ino:L5861-L5922`): `moove_Distance_R(dist, 15, 10)`, dist = 100/250/450 по длине шаттла; `motor_Stop`; `lifter_Down`; `motor_Start_Forward`; цикл вперёд до (R1&&R2) или `distance[1] < 90 + chnlOffset` (конец канала спереди) с 50 мс pace, `delay(5)` повторным опросом и скоростью 10; `motor_Stop`; `lifter_Up`; `diffPallete = 0`.
11. Доставка к месту выгрузки: `stop_Before_Pallete_R()` (`Cntrl_V2/Cntrl_V2.ino:L5924`), затем abort-проверка (`Cntrl_V2/Cntrl_V2.ino:L5925-L5926`).
12. Опускание и bookkeeping: `if (!longWork && lifterUp) lifter_Down()` + `lastPallete = 1`, `lastPalletePosition = currentPosition` (`Cntrl_V2/Cntrl_V2.ino:L5928-L5933`); лог позиции; `loadCounter++` (`Cntrl_V2/Cntrl_V2.ino:L5938`).
13. Возврат в `single_Load`: безусловный `moove_Forward()` (`Cntrl_V2/Cntrl_V2.ino:L5999`); возврат в `run_Cmd`: `status = CMD_STOP`, `send_Cmd()` (`Cntrl_V2/Cntrl_V2.ino:L3270-L3271`).

Условия циклов и выходы зафиксированы в шагах; цикл поиска вперёд не имеет выхода по «паллета не найдена», кроме timeout `2000000/maxSpeed` и `distance[1] < 80` (`Cntrl_V2/Cntrl_V2.ino:L5802`).

**Stop/fault/abort**

- `shouldAbortLoop()` (`Cntrl_V2/Cntrl_V2.ino:L8598-L8601`) проверяется в голове всех циклов движения; в board-delay цикле и в `moove_Distance_*` перед стопом вызывается `preserveManualStopOnAbort()` (`Cntrl_V2/Cntrl_V2.ino:L8585-L8591`, вызовы `Cntrl_V2/Cntrl_V2.ino:L5754`, `Cntrl_V2/Cntrl_V2.ino:L5832`, `Cntrl_V2/Cntrl_V2.ino:L4843`, `Cntrl_V2/Cntrl_V2.ino:L5044`) - сохраняет CMD_STOP_MANUAL, иначе ставит CMD_STOP.
- Обработчики abort в `load_Pallete` ставят `status = CMD_STOP` + `motor_Stop()` (`Cntrl_V2/Cntrl_V2.ino:L5711-L5716`, `Cntrl_V2/Cntrl_V2.ino:L5877-L5882`); в `single_Load`-поиске аналогично (`Cntrl_V2/Cntrl_V2.ino:L5964-L5969`). Warning-ветви (WARN_PALLET_SIZE_ERROR при 800) не останавливают операцию и не делают return - управление проваливается к финальному `moove_Forward()` (`Cntrl_V2/Cntrl_V2.ino:L5952-L5956`, `Cntrl_V2/Cntrl_V2.ino:L5999`).
- Fault-пути лифтера: `lifter_Up`/`lifter_Down` по таймауту `lifterDelay` -> `lifter_Stop()` + `setFault(FAULT_LIFTER_TIMEOUT)` + `status = CMD_STOP` (`Cntrl_V2/Cntrl_V2.ino:L2412-L2419`, `Cntrl_V2/Cntrl_V2.ino:L2501-L2508`); abort внутри подъёма/опускания -> `preserveManualStopOnAbort()` + return без остановки движения (`Cntrl_V2/Cntrl_V2.ino:L2388-L2392`, `Cntrl_V2/Cntrl_V2.ino:L2407-L2411`, `Cntrl_V2/Cntrl_V2.ino:L2477-L2481`, `Cntrl_V2/Cntrl_V2.ino:L2496-L2500`).
- Отказ ToF: `motor_Start_Forward/Reverse` не выставляют `motorStart`, если каналный ToF не ready - `ensureChannelTofReadyForMotion` латчит FAULT_TOF_CH_F/R и возвращает false (`Cntrl_V2/Cntrl_V2.ino:L2239-L2258`, `Cntrl_V2/Cntrl_V2.ino:L3536-L3553`); последующие `motor_Speed` сразу возвращаются (`motorReverse == 2`, `Cntrl_V2/Cntrl_V2.ino:L2090-L2091`), циклы поиска крутятся до timeout (поиск досок) или до abort пользователем (обратный поиск `single_Load` - выхода по времени нет). Во время движения свежесть ToF контролирует `enforceActiveMotionTofSafety` -> `motor_Force_Stop()` + fault (`Cntrl_V2/Cntrl_V2.ino:L3555-L3571`, вызывается из `get_Distance` `Cntrl_V2/Cntrl_V2.ino:L3873`, `Cntrl_V2/Cntrl_V2.ino:L3892`, `Cntrl_V2/Cntrl_V2.ino:L3917`).
- Пробуксовка: `blink_Work` через 1.5 с отсутствия движения -> `motor_Stop` + CMD_STOP + `setFault(FAULT_MOTOR_STALL)` (`Cntrl_V2/Cntrl_V2.ino:L4065-L4085`).
- `FAULT_MOVE_TIMEOUT` в `moove_Distance_F/R` при 5 с без прогресса -> `motor_Stop` + `lifter_Down` + CMD_STOP (`Cntrl_V2/Cntrl_V2.ino:L4982-L4989`, `Cntrl_V2/Cntrl_V2.ino:L5182-L5189`); AS5600 недоступен -> тихий return/стоп (`Cntrl_V2/Cntrl_V2.ino:L4813-L4816`, `Cntrl_V2/Cntrl_V2.ino:L4852-L4857`, `Cntrl_V2/Cntrl_V2.ino:L5014-L5017`, `Cntrl_V2/Cntrl_V2.ino:L5053-L5058`).
- Любой active fault переводит верхний уровень в `CoreOpMode::ERROR` в следующей итерации `loop()` (`Cntrl_V2/Cntrl_V2.ino:L1819-L1820`, `Cntrl_V2/Cntrl_V2.ino:L2050-L2078`) с принудительным `motor_Force_Stop()`.
- `SystemYield()` вызывается во всех циклах движения и в `lifter_*`; НЕ вызывается напрямую в `detect_Pallete` (блокирующие `delay(5)`, `Cntrl_V2/Cntrl_V2.ino:L3414-L3462`), в `delay(5)` повторного опроса recapture (`Cntrl_V2/Cntrl_V2.ino:L5903`), в рамповых `delay(...)` внутри `motor_Stop`/`motor_Speed` (`Cntrl_V2/Cntrl_V2.ino:L2289-L2327`; в `motor_Speed` рампа yield есть - `Cntrl_V2/Cntrl_V2.ino:L2129`, `Cntrl_V2/Cntrl_V2.ino:L2180`).

**Observable outcomes**

- Успех: паллета поднята, перевезена к концу канала (reverse), опущена; `lastPallete = 1`, `lastPalletePosition = currentPosition`; `sramStats->payload.loadCounter++` (`Cntrl_V2/Cntrl_V2.ino:L5938`), попутно `liftDownCounter++`/`liftUpCounter++` (`Cntrl_V2/Cntrl_V2.ino:L2520`, `Cntrl_V2/Cntrl_V2.ino:L2449`); финальный `moove_Forward()` к началу канала; `status = CMD_STOP`, `currentOperation` STATE_LOAD -> STATE_IDLE (`Cntrl_V2/Cntrl_V2.ino:L1906`), телеметрия на границах (`send_Cmd`).
- Отказы: WARN_PALLET_SIZE_ERROR (`Cntrl_V2/Cntrl_V2.ino:L5955`, `Cntrl_V2/Cntrl_V2.ino:L5990`, `Cntrl_V2/Cntrl_V2.ino:L5780`), WARN_PALLET_NOT_FOUND (`Cntrl_V2/Cntrl_V2.ino:L5996`), WARN_OBSTACLE_AHEAD (`Cntrl_V2/Cntrl_V2.ino:L5807`, `Cntrl_V2/Cntrl_V2.ino:L5820`, `Cntrl_V2/Cntrl_V2.ino:L5847`), WARN_CHANNEL_FULL (`Cntrl_V2/Cntrl_V2.ino:L5674`) - все с таймаутом 5000 мс (`Cntrl_V2/Cntrl_V2.ino:L550-L553`); warning не прерывает приём новых команд и не является error.
- Логи: "Start loading pallete..." (`Cntrl_V2/Cntrl_V2.ino:L5664`), "Single load fail..." (`Cntrl_V2/Cntrl_V2.ino:L5995`), "Last pallete position after load = %d" (`Cntrl_V2/Cntrl_V2.ino:L5936`).
- `palleteCount`/`palletePosition[]` загрузкой НЕ обновляются (только `pallete_Counting_F`, `Cntrl_V2/Cntrl_V2.ino:L6003-L6135`; в телеметрии `Cntrl_V2/Cntrl_V2.ino:L8834`).

**Timing conditions**

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| dst = 600 (670 для 1200) мм | `Cntrl_V2/Cntrl_V2.ino:L5724-L5726` | configured | фиксированный заезд после первой доски |
| maxbb = 3 + (150 - maxSpeed)/10; +3 при distance[1] < 300; -3 для 1200 (до 0) | `Cntrl_V2/Cntrl_V2.ino:L5734-L5744` | configured | число 100-мс шагов доезда под последнюю доску |
| 100 мс на шаг board-delay | `Cntrl_V2/Cntrl_V2.ino:L5748` | configured | шаг ожидания в цикле доезда |
| timeout поиска = 2000000 / maxSpeed мс (20833 мс при default maxSpeed=96) | `Cntrl_V2/Cntrl_V2.ino:L5802` | configured (формула); 20833 - inferred | полный таймаут поиска паллеты вперёд |
| 50 мс pace обновления скорости | `Cntrl_V2/Cntrl_V2.ino:L5786` | configured | период пересчёта скорости в поиске |
| зона обратного поиска distance[1] < 400 + chnlOffset | `Cntrl_V2/Cntrl_V2.ino:L5961` | configured | граница поиска в `single_Load` от начала канала |
| recapture dist = 100/250/450 мм, скорость 15, min 10 | `Cntrl_V2/Cntrl_V2.ino:L5864-L5869` | configured | перехват паллеты после подъёма |
| порог конца канала 90 + chnlOffset (creep 5 при CMD_LOAD и distance[1] > 80) | `Cntrl_V2/Cntrl_V2.ino:L5246-L5256` | configured | останов у начала канала |
| otstup = 70 (100 при lifterUp) + chnlOffset | `Cntrl_V2/Cntrl_V2.ino:L4526-L4529` | configured | минимальная дистанция при доводке в stop_Before_Pallete_R |
| 4 измерения по 100 мс перед финальной доводкой | `Cntrl_V2/Cntrl_V2.ino:L4564-L4579` | configured | стабилизация замера дистанции |
| формула остановки: dist/4 + diffPallete - interPalleteDistance - 100 - diff - mprOffset, -25 для 1000/1200, *0.96 | `Cntrl_V2/Cntrl_V2.ino:L4626-L4630` | configured | расчёт доводки перед паллетом |
| lifterDelay = 3800 мс | `Cntrl_V2/Cntrl_V2.ino:L1646`, `Cntrl_V2/Cntrl_V2.ino:L2501` | configured | таймаут лифтера |
| debounce detect_Pallete: delay(5) до 4 раз | `Cntrl_V2/Cntrl_V2.ino:L3442-L3462` | configured | антидребезг pallet GPIO |
| 50 мс троттлинг CAN-команд скорости | `Cntrl_V2/Cntrl_V2.ino:L2090-L2092` | configured | период записи скорости на привод |
| 5 с без прогресса -> FAULT_MOVE_TIMEOUT | `Cntrl_V2/Cntrl_V2.ino:L4982-L4989`, `Cntrl_V2/Cntrl_V2.ino:L5182-L5189` | configured | таймаут фиксированного перемещения |
| 1.5 с без движения -> FAULT_MOTOR_STALL | `Cntrl_V2/Cntrl_V2.ino:L4078-L4085` | configured | детект пробуксовки |
| коррекция dist: -50 (>500 мм), -10 (>50 мм) | `Cntrl_V2/Cntrl_V2.ino:L4830-L4833`, `Cntrl_V2/Cntrl_V2.ino:L5027-L5030` | configured | упреждение торможения в moove_Distance_* |

**Resource effects**

- CAN: ID 100 - скорость/стоп привода движения (`Cntrl_V2/Cntrl_V2.ino:L2096`, `Cntrl_V2/Cntrl_V2.ino:L2267`); ID 101 - лифтер (`Cntrl_V2/Cntrl_V2.ino:L2374`, `Cntrl_V2/Cntrl_V2.ino:L2463`); ID 2405 - чтение тока лифтера только в `lifter_Up` (`Cntrl_V2/Cntrl_V2.ino:L2427-L2435`).
- I2C: ToF - round-robin 4 сенсора с шагом >= 8 мс и паузой на BMS TX (`Cntrl_V2/Cntrl_V2.ino:L3861-L3871`); AS5600 углы в `set_Position`/`moove_Distance_*` (`Cntrl_V2/Cntrl_V2.ino:L3925`, `Cntrl_V2/Cntrl_V2.ino:L4813`).
- GPIO: DATCHIK_F1/F2/R1/R2 (`Cntrl_V2/Cntrl_V2.ino:L3406-L3464`), концевики DL_UP/DL_DOWN (`Cntrl_V2/Cntrl_V2.ino:L2363`, `Cntrl_V2/Cntrl_V2.ino:L2455`), CHANNEL при admission (`Cntrl_V2/Cntrl_V2.ino:L2830`), LED через `blink_Work`/`blink_Warning`.
- UART: ACK в порт-источник (radio/display, `Cntrl_V2/Cntrl_V2.ino:L2697-L2711`), телеметрия в SerialDisplay по `send_Cmd` на границах операции.
- Flash/логи: `makeLog` по шагам; надёжные логи `makeReliableLog` в ToF-подсистеме (вне тела операции).
- Блокирующие задержки без yield: `detect_Pallete` (`delay(5)`), `delay(5)` в recapture, рамповые delay в `motor_Stop`/`lifter_*`.

**Профильные варианты 800/1000/1200**

- pickup/board distance: 600 мм; 1200 -> 670 мм (`Cntrl_V2/Cntrl_V2.ino:L5724-L5726`); для 800 укорочение закомментировано (`Cntrl_V2/Cntrl_V2.ino:L5728`).
- recapture: 1000 -> 250, 1200 -> 450, иначе 100 (`Cntrl_V2/Cntrl_V2.ino:L5864-L5868`).
- board-delay: база `3 + (150-maxSpeed)/10`, для 1200 минус 3 шага (`Cntrl_V2/Cntrl_V2.ino:L5734-L5744`).
- 800: при single-load требуются ОБЕ пары датчиков (все 4); только F-пара -> WARN_PALLET_SIZE_ERROR, `load_Pallete()` не вызывается (`Cntrl_V2/Cntrl_V2.ino:L5949-L5956`, `Cntrl_V2/Cntrl_V2.ino:L5981-L5991`). Для 1000/1200 достаточно F-пары.
- pltMaxLn = shuttleLength - 20 для всех профилей; профильные поправки закомментированы (`Cntrl_V2/Cntrl_V2.ino:L5771-L5774`).
- -25 к доводке только для 1000/1200 (`Cntrl_V2/Cntrl_V2.ino:L4627-L4628`).

**Unknowns**

- Физический смысл и калибровка констант 600/670, 400, порога `distance[3] > 750`, множителя 0.96 - из source не устанавливается.
- Реальные runtime-значения maxSpeed/shuttleLength/chnlOffset/mprOffset/interPalleteDistance берутся из EEPROM (default: maxSpeed=96 `Cntrl_V2/Cntrl_V2.ino:L591`, shuttleLength=1000 `Cntrl_V2/Cntrl_V2.ino:L598`, interPalleteDistance=100 `Cntrl_V2/Cntrl_V2.ino:L597`).
- `int cnt = millis()` (`Cntrl_V2/Cntrl_V2.ino:L5707`) и глобальный `int count` (`Cntrl_V2/Cntrl_V2.ino:L586`): поведение после переполнения int32 (~24.8 сут аптайма) не специфицировано; паттерн сквозной.

**Disposition proposals (только предложения)**

- Preserve: структура «подход -> заезд под паллету по первой доске -> board-delay -> подъём -> recapture -> доставка -> опускание»; это наблюдаемое продуктовое поведение.
- Preserve: профильные константы 600/670/500(unload), recapture 100/250/450, -25, требование обеих пар для 800 - как verified production behavior (anchors выше).
- Change: timeout поиска `2000000/maxSpeed` - защита от maxSpeed==0 и невалидных значений (в V1 CFG_MAX_SPEED пишется без валидации, `Cntrl_V2/Cntrl_V2.ino:L2955-L2961`; деление на ноль на ARM - fault).
- Change: обратный поиск `single_Load` без таймаута - при тихом отказе `motor_Start_Reverse` цикл не выходит (`Cntrl_V2/Cntrl_V2.ino:L5959-L5979`); нужен watchdog выхода.
- Change: board-delay `maxbb` при maxSpeed > 170/180 даёт отрицательное значение, заворачиваемое в uint8_t (253 итераций) - нужен clamp (`Cntrl_V2/Cntrl_V2.ino:L5734`).
- Change: warning-ветви 800 без return продолжаются финальным `moove_Forward` - неоднозначимый исход операции.
- Preserve/unknown: проверка WARN_CHANNEL_FULL через `lastPalletePosition` для одиночной загрузки фактически не работает (обнуление при приёме команды, `Cntrl_V2/Cntrl_V2.ino:L1845`) - решение о нужности защиты требует продукта.

---

### UnloadPallet (CMD_UNLOAD = 0x21)

**Entry points и call sites**

- Объявление: `Cntrl_V2/ShuttleProtocol.h:L107`. Supported: `Cntrl_V2/Cntrl_V2.ino:L8370`. Маппинг STATE_UNLOAD: `Cntrl_V2/Cntrl_V2.ino:L8235-L8236`.
- Авто-путь: dispatch `Cntrl_V2/Cntrl_V2.ino:L3273-L3283`: `send_Cmd()` -> `lifter_Down()` -> `moove_Forward()` -> `unload_Pallete()` -> `status = CMD_MOVE_RIGHT_MAN` -> `moove_Forward()` -> `status = CMD_STOP` -> `send_Cmd()`.
- Факт ordering: в ветви CMD_UNLOAD нет guard'а перед финальным `moove_Forward()`; у CMD_LONG_UNLOAD guard есть - `if ((status == CMD_STOP && distance[1] > 100) || isErrorActive()) return;` (`Cntrl_V2/Cntrl_V2.ino:L3367-L3368`, аналог для QTY `Cntrl_V2/Cntrl_V2.ino:L3384-L3385`). После abort внутри `unload_Pallete` (status=CMD_STOP без error) dispatch всё равно перезаписывает status=CMD_MOVE_RIGHT_MAN и выполняет движение к началу канала; при active error `moove_Forward` сразу выходит через `shouldAbortLoop` (`Cntrl_V2/Cntrl_V2.ino:L5216-L5221`).
- Ручной путь: `Cntrl_V2/Cntrl_V2.ino:L1996-L2000` (см. LoadPallet).
- `unload_Pallete()` (`Cntrl_V2/Cntrl_V2.ino:L5329-L5659`) вызывает: `fifoLifo_Inverse` (`Cntrl_V2/Cntrl_V2.ino:L4037-L4048`), `lifter_Down`/`lifter_Up`, `get_Distance`, `moove_Before_Pallete_R` (`Cntrl_V2/Cntrl_V2.ino:L4640-L4793`), `motor_Start_Reverse`/`motor_Speed`/`motor_Stop`, `detect_Pallete`, `set_Position` (`Cntrl_V2/Cntrl_V2.ino:L3922-L4034`), `moove_Distance_R`/`moove_Distance_F`, `moove_Before_Pallete_F` (`Cntrl_V2/Cntrl_V2.ino:L4410-L4515`), в else-ветви (compact-контекст) `stop_Before_Pallete_F` (`Cntrl_V2/Cntrl_V2.ino:L4292-L4407`).
- Косвенные вызовы `unload_Pallete` вне CMD_UNLOAD: `pallete_Compacting_R` (`Cntrl_V2/Cntrl_V2.ino:L6218`), `long_Unload` (`Cntrl_V2/Cntrl_V2.ino:L6386`, `Cntrl_V2/Cntrl_V2.ino:L6479`), demo (`Cntrl_V2/Cntrl_V2.ino:L6795`).

**Admission/preconditions**

- Протокольный admission - общий блок выше (идентичен CMD_LOAD).
- Внутренних preconditions-проверок у `unload_Pallete` нет (в отличие от `load_Pallete`); единственное условие входа в фазу подхода - `distance[2] > 750` (`Cntrl_V2/Cntrl_V2.ino:L5345-L5349`).

**Шаги и переходы (normal path)**

1. `run_Cmd`: `send_Cmd()`, `lifter_Down()`, `moove_Forward()` - подвод к началу канала/последней паллете (`Cntrl_V2/Cntrl_V2.ino:L3275-L3277`).
2. Вход `unload_Pallete`: лог "Start unloading pallete..."; при `fifoLifo` - `fifoLifo_Inverse()` (инверсия направления, см. раздел FIFO/LIFO) (`Cntrl_V2/Cntrl_V2.ino:L5331-L5333`); `startDiff = 0`, `lifter_Down()`, `get_Distance()`; `startDiff = 20` при `distance[0] < 90 + chnlOffset` (`Cntrl_V2/Cntrl_V2.ino:L5338-L5342`).
3. Подход: при `distance[2] > 750` - `moove_Before_Pallete_R()` (движение к концу канала до зоны паллеты, выход по `distance[2] < 1000` или `distance[0] <= 90 + chnlOffset`, `Cntrl_V2/Cntrl_V2.ino:L4653-L4784`); abort -> `oldSpeed = 0`, восстановление fifoLifo, return (`Cntrl_V2/Cntrl_V2.ino:L5350-L5356`).
4. Поиск досок назад: `motor_Start_Reverse()`, скорость `oldSpeed` (если >20 или `distance[0] < 250 + chnlOffset`), иначе 28 (`Cntrl_V2/Cntrl_V2.ino:L5358-L5362`); цикл `while (moove)` с `SystemYield()` в голове, abort -> `motor_Stop` + CMD_STOP + восстановление fifoLifo + return (`Cntrl_V2/Cntrl_V2.ino:L5364-L5374`).
5. Первая доска (R1&&R2 && !frontBoard): `set_Position`, фиксация `currentPalletePosition`, `moove_Distance_R(dst, oldSpeed, 10)`: dst = 600; 1200 -> 670; 800 -> 500 при `channelLength - currentPosition - shuttleLength < 1500` (`Cntrl_V2/Cntrl_V2.ino:L5378-L5388`); `frontBoard = 1`.
6. Board-delay (R1&&R2 && frontBoard): `maxbb = 2 + (150 - maxSpeed)/10`; +3 при `distance[0] < 300`; для 1200 -3 (до 0) (`Cntrl_V2/Cntrl_V2.ino:L5392-L5402`); цикл шагов по 100 мс (`Cntrl_V2/Cntrl_V2.ino:L5403-L5426`) с `SystemYield`/abort/`preserveManualStopOnAbort`, `palleteLenght = abs(currentPalletePosition - currentPosition)`, ранний выход при (F1&&F2); затем `moove = 0`.
7. Проверка длины: `pltMaxLn = shuttleLength - 20`; если `frontBoard && palleteLenght >= pltMaxLn` (задняя доска не увидена за длину шаттла) -> `motor_Stop`, лог "Pallete error in BB...", WARN_PALLET_SIZE_ERROR, `moove_Forward()`, status=CMD_STOP, return (`Cntrl_V2/Cntrl_V2.ino:L5427-L5441`).
8. 50 мс pace-ветвь: скорость `distance[0]/23` (clamp 5..oldSpeed); timeout `millis() - cnt > 2000000 / maxSpeed || distance[0] < 80` -> лог "Pallete error...", WARN_OBSTACLE_AHEAD, восстановление fifoLifo, return БЕЗ смены status (`Cntrl_V2/Cntrl_V2.ino:L5444-L5464`).
9. После цикла: `set_Position`, лог длины паллеты, abort-проверка (`Cntrl_V2/Cntrl_V2.ino:L5467-L5474`).
10. Проверка места впереди: `get_Distance`; при `distance[3] < 900` (впереди ближе 900 мм что-то есть) -> `lifter_Down`, WARN_PALLET_NOT_FOUND, `moove_Forward()`, CMD_STOP, return (`Cntrl_V2/Cntrl_V2.ino:L5476-L5485`).
11. Подъём: `lifter_Up()` (`Cntrl_V2/Cntrl_V2.ino:L5486`); bookkeeping-захват: `pstn = currentPosition` при `distance[2] < 600` (паллета сзади ближе 600 мм) (`Cntrl_V2/Cntrl_V2.ino:L5487-L5489`).
12. Recapture при `!F1 || !F2` (`Cntrl_V2/Cntrl_V2.ino:L5490-L5554`): `moove_Distance_F(dist, 12, 10)`, dist = 100/250/450; `motor_Stop`; `lifter_Down`; цикл назад (`motor_Start_Reverse`) до (F1&&F2) или `distance[0] < 90 + chnlOffset` (во втором случае: пересчёт `channelLength = currentPosition + shuttleLength + distance[0] - 30`, status=CMD_STOP, `endOfChannel = 1`); `motor_Stop`; `lifter_Up`.
13. Доставка к началу канала - только при `status in {CMD_UNLOAD, CMD_LONG_UNLOAD, CMD_LONG_UNLOAD_QTY}` (`Cntrl_V2/Cntrl_V2.ino:L5555`; для compact-вызовов else-ветвь `stop_Before_Pallete_F()` `Cntrl_V2/Cntrl_V2.ino:L5631-L5634`): `moove_Before_Pallete_F()`; `motor_Stop`; при `distance[1] > 150`: цикл ожидания `distance[3] >= 800` (без таймаута, только abort-выход, `Cntrl_V2/Cntrl_V2.ino:L5561-L5574`), затем ожидание `millis() - count < waitTime` (`Cntrl_V2/Cntrl_V2.ino:L5575-L5588`), затем creep вперёд `while (distance[1] > 90 + chnlOffset)` со скоростью 20 -> `distance[1]/20` -> 6 и защитным стопом `distance[3] < 600 && distance[1] > distance[3]` (`Cntrl_V2/Cntrl_V2.ino:L5589-L5626`); placement `currentPosition = distance[1] - 30` (`Cntrl_V2/Cntrl_V2.ino:L5627`).
14. Опускание и bookkeeping: abort-проверка (`Cntrl_V2/Cntrl_V2.ino:L5635-L5640`); `if (!longWork && lifterUp) lifter_Down()` (`Cntrl_V2/Cntrl_V2.ino:L5642-L5643`); `lastPalletePosition = pstn + 800 + interPalleteDistance` при `palleteLenght < 850 && pstn`, иначе `pstn + 1000 + interPalleteDistance`; `lastPallete = (pstn != 0)` (`Cntrl_V2/Cntrl_V2.ino:L5644-L5651`); `unloadCounter++` (`Cntrl_V2/Cntrl_V2.ino:L5656`); восстановление fifoLifo (`Cntrl_V2/Cntrl_V2.ino:L5657-L5658`).
15. Возврат в dispatch: `status = CMD_MOVE_RIGHT_MAN`, `moove_Forward()` (движение к началу канала), `status = CMD_STOP`, `send_Cmd()` (`Cntrl_V2/Cntrl_V2.ino:L3279-L3282`).

**Stop/fault/abort**

- Все циклы имеют `shouldAbortLoop()` в голове; abort-ветви в основном ставят `status = CMD_STOP` + `motor_Stop()` + восстановление fifoLifo + return (`Cntrl_V2/Cntrl_V2.ino:L5367-L5374`, `Cntrl_V2/Cntrl_V2.ino:L5506-L5513`, `Cntrl_V2/Cntrl_V2.ino:L5564-L5571`, `Cntrl_V2/Cntrl_V2.ino:L5580-L5587`, `Cntrl_V2/Cntrl_V2.ino:L5594-L5601`); в board-delay - `preserveManualStopOnAbort()` БЕЗ принудительного CMD_STOP (`Cntrl_V2/Cntrl_V2.ino:L5410-L5415`).
- Warning-выходы (пункты 7, 8, 10) не выставляют error: WARN_PALLET_SIZE_ERROR, WARN_OBSTACLE_AHEAD, WARN_PALLET_NOT_FOUND (все 5000 мс).
- Factual: при CMD_STOP из warning-ветвей и из abort-ветвей dispatch CMD_UNLOAD всё равно выполняет `status = CMD_MOVE_RIGHT_MAN; moove_Forward()` (`Cntrl_V2/Cntrl_V2.ino:L3279-L3280`) - шаттл доезжает к началу канала, если нет active error (см. Entry points).
- Fault лифтера, ToF, AS5600, FAULT_MOVE_TIMEOUT, FAULT_MOTOR_STALL - как в LoadPallet (`Cntrl_V2/Cntrl_V2.ino:L2412-L2419`, `Cntrl_V2/Cntrl_V2.ino:L3536-L3571`, `Cntrl_V2/Cntrl_V2.ino:L5182-L5189`, `Cntrl_V2/Cntrl_V2.ino:L4078-L4085`).
- Специфика `motor_Stop` для UNLOAD: при `(status == CMD_UNLOAD || CMD_LONG_UNLOAD) && lifterUp && distance[3] + 100 < distance[1]` рампа использует `delay(10)` (`Cntrl_V2/Cntrl_V2.ino:L2288-L2289`) и после стопа отправляется 10 повторных zero-speed CAN-пакетов с паузами по 100 мс вместо одного (`Cntrl_V2/Cntrl_V2.ino:L2305-L2316`); в этих 100-мс паузах `SystemYield` не вызывается напрямую (только `blink_Work`, `Cntrl_V2/Cntrl_V2.ino:L2307-L2315`).
- `SystemYield` отсутствует напрямую: `detect_Pallete` debounce, `delay(5)` повторного опроса в recapture (`Cntrl_V2/Cntrl_V2.ino:L5536`), рамповые delay `motor_Stop`.

**Observable outcomes**

- Успех: паллета снята из глубины канала, поднята, доставлена к началу канала, опущена; шаттл в начале канала (`currentPosition = distance[1] - 30`, затем финальный `moove_Forward` -> `currentPosition = 60`); `unloadCounter++` (`Cntrl_V2/Cntrl_V2.ino:L5656`), `liftUpCounter++`/`liftDownCounter++`; `lastPallete`/`lastPalletePosition` только при наличии паллеты сзади (`pstn != 0`, `Cntrl_V2/Cntrl_V2.ino:L5644-L5651`); status=CMD_STOP; currentOperation STATE_UNLOAD -> STATE_IDLE.
- Отказы: WARN_PALLET_SIZE_ERROR (`Cntrl_V2/Cntrl_V2.ino:L5437`), WARN_OBSTACLE_AHEAD (`Cntrl_V2/Cntrl_V2.ino:L5458`), WARN_PALLET_NOT_FOUND (`Cntrl_V2/Cntrl_V2.ino:L5481`); логи "Pallete error in BB..." (`Cntrl_V2/Cntrl_V2.ino:L5435`), "Pallete error..." (`Cntrl_V2/Cntrl_V2.ino:L5455`), "Pallete lenght = %d" (`Cntrl_V2/Cntrl_V2.ino:L5468`).
- `palleteCount`/`palletePosition[]` не затрагиваются.
- `firstPalletePosition` операцией не пишется; сбрасывается в DEMO/COMPACT_R (`Cntrl_V2/Cntrl_V2.ino:L3311`, `Cntrl_V2/Cntrl_V2.ino:L3343`), пишется в compact/demo (`Cntrl_V2/Cntrl_V2.ino:L6229`, `Cntrl_V2/Cntrl_V2.ino:L6796`); читается в `moove_Before_Pallete_F` для профиля скорости при `lifterUp` (`Cntrl_V2/Cntrl_V2.ino:L4438-L4443`) - т.е. остаточное значение от предыдущего compact влияет на скорость доставки unload.

**Timing conditions**

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| dst = 600 (1200 -> 670; 800 -> 500 у конца канала) мм | `Cntrl_V2/Cntrl_V2.ino:L5382-L5386` | configured | заезд после первой доски |
| условие 800-варианта: channelLength - currentPosition - shuttleLength < 1500 | `Cntrl_V2/Cntrl_V2.ino:L5385` | configured | близость конца канала |
| maxbb = 2 + (150 - maxSpeed)/10; +3 при distance[0] < 300; -3 для 1200 (до 0) | `Cntrl_V2/Cntrl_V2.ino:L5392-L5402` | configured | число 100-мс шагов доезда |
| 100 мс на шаг board-delay | `Cntrl_V2/Cntrl_V2.ino:L5406` | configured | шаг ожидания доезда под доску |
| timeout поиска = 2000000 / maxSpeed мс (20833 мс при maxSpeed=96) | `Cntrl_V2/Cntrl_V2.ino:L5453` | configured (формула); 20833 - inferred | таймаут поиска паллеты назад |
| 50 мс pace скорости | `Cntrl_V2/Cntrl_V2.ino:L5444` | configured | период пересчёта скорости |
| recapture dist = 100/250/450 мм, скорость 12, min 10 | `Cntrl_V2/Cntrl_V2.ino:L5493-L5498` | configured | перехват после подъёма |
| gate места впереди: distance[3] < 900 -> отказ | `Cntrl_V2/Cntrl_V2.ino:L5477` | configured | «некуда везти» |
| pstn-захват: distance[2] < 600 | `Cntrl_V2/Cntrl_V2.ino:L5488` | configured | паллета сзади близко |
| ожидание distance[3] >= 800 | `Cntrl_V2/Cntrl_V2.ino:L5561` | configured (без таймаута) | освобождение направления впереди |
| waitTime: default 15000, clamp 5000..30000 мс | `Cntrl_V2/Cntrl_V2.ino:L585`, `Cntrl_V2/Cntrl_V2.ino:L7653-L7656`, `Cntrl_V2/Cntrl_V2.ino:L5575` | configured | пауза перед финальным подъездом |
| creep вперёд: 20 -> distance[1]/20 -> 6; стоп при distance[3] < 600 && distance[1] > distance[3] | `Cntrl_V2/Cntrl_V2.ino:L5589-L5617` | configured | профиль доводки к началу канала |
| placement: currentPosition = distance[1] - 30 | `Cntrl_V2/Cntrl_V2.ino:L5627` | configured | позиция после выгрузки |
| порог конца канала 90 + chnlOffset; зона подхода distance[0] < 250 + chnlOffset | `Cntrl_V2/Cntrl_V2.ino:L5341`, `Cntrl_V2/Cntrl_V2.ino:L5359` | configured | границы канала |
| формула остановки F: dist/4 - interPalleteDistance - 100 - diff - mprOffset, -25 для 1000/1200, *0.96 | `Cntrl_V2/Cntrl_V2.ino:L4395-L4400` | configured | доводка в stop_Before_Pallete_F (else-ветвь/compact) |
| 10 x 100 мс повторных CAN-стопоv при UNLOAD+lifterUp | `Cntrl_V2/Cntrl_V2.ino:L2305-L2316` | configured | усиленная доводка/стоп с грузом |
| lifterDelay = 3800 мс; debounce 5 мс; CAN-троттлинг 50 мс | `Cntrl_V2/Cntrl_V2.ino:L1646`, `Cntrl_V2/Cntrl_V2.ino:L3442-L3462`, `Cntrl_V2/Cntrl_V2.ino:L2090` | configured | как в LoadPallet |

**Resource effects**

- CAN: ID 100 привод, ID 101 лифтер, ID 2405 ток (в `lifter_Up`); расширенный цикл стоп-пакетов для UNLOAD (`Cntrl_V2/Cntrl_V2.ino:L2305-L2316`).
- I2C: ToF round-robin + AS5600 - как в LoadPallet.
- GPIO: pallet-датчики (инверсия F/R при `inverse`, `Cntrl_V2/Cntrl_V2.ino:L3408-L3435`), концевики, LED.
- UART: ACK + телеметрия на границах (`Cntrl_V2/Cntrl_V2.ino:L3275`, `Cntrl_V2/Cntrl_V2.ino:L3282`).
- Flash/логи: makeLog по шагам.
- Блокирующие задержки без прямого yield: detect_Pallete debounce, `delay(5)` recapture, 100-мс паузы расширенного стопа, рамповые delay.

**Профильные варианты 800/1000/1200**

- pickup distance: 600; 1200 -> 670; 800 -> 500 при остатке канала < 1500 мм (`Cntrl_V2/Cntrl_V2.ino:L5382-L5386`).
- recapture: 1000 -> 250, 1200 -> 450, иначе 100 (`Cntrl_V2/Cntrl_V2.ino:L5493-L5497`).
- board-delay: база `2 + (150-maxSpeed)/10` (на 1 шаг меньше, чем у load), 1200 -> -3 (`Cntrl_V2/Cntrl_V2.ino:L5392-L5402`).
- -25 к доводке stop_Before_Pallete_* только для 1000/1200 (`Cntrl_V2/Cntrl_V2.ino:L4397-L4398`, `Cntrl_V2/Cntrl_V2.ino:L4627-L4628`).
- 800: специальных требований к парам датчиков в unload нет (обе пары проверяются симметрично всем профилям в условии первой/последней доски, `Cntrl_V2/Cntrl_V2.ino:L5378`, `Cntrl_V2/Cntrl_V2.ino:L5390`); профильное различие - только dst=500.
- pltMaxLn = shuttleLength - 20; профильные поправки закомментированы (`Cntrl_V2/Cntrl_V2.ino:L5428-L5431`).

**Взаимодействие с inverse/FIFO/LIFO**

- `unload_Pallete` при `fifoLifo` вызывает `fifoLifo_Inverse()` на входе и во ВСЕХ выходах (`Cntrl_V2/Cntrl_V2.ino:L5332-L5333`, `Cntrl_V2/Cntrl_V2.ino:L5353-L5354`, `Cntrl_V2/Cntrl_V2.ino:L5371-L5372`, `Cntrl_V2/Cntrl_V2.ino:L5459-L5460`, `Cntrl_V2/Cntrl_V2.ino:L5471-L5472`, `Cntrl_V2/Cntrl_V2.ino:L5510-L5511`, `Cntrl_V2/Cntrl_V2.ino:L5568-L5569`, `Cntrl_V2/Cntrl_V2.ino:L5584-L5585`, `Cntrl_V2/Cntrl_V2.ino:L5598-L5599`, `Cntrl_V2/Cntrl_V2.ino:L5637-L5638`, `Cntrl_V2/Cntrl_V2.ino:L5657-L5658`).
- `fifoLifo_Inverse` (`Cntrl_V2/Cntrl_V2.ino:L4037-L4048`): при выходе из inverse пересчитывает `currentPosition = channelLength - currentPosition - 800` (константа 800 не зависит от shuttleLength); при входе в inverse позицию НЕ пересчитывает - асимметрия.
- Инверсия действует на: маппинг pallet GPIO (`Cntrl_V2/Cntrl_V2.ino:L3408-L3435`), слоты ToF (`Cntrl_V2/Cntrl_V2.ino:L3483-L3503`), знак скорости `motorReverse ^ inverse` (`Cntrl_V2/Cntrl_V2.ino:L2113`), выбор таблицы энкодера в `moove_Distance_*` (`Cntrl_V2/Cntrl_V2.ino:L4859`, `Cntrl_V2/Cntrl_V2.ino:L5060`).
- Факт: `single_Load`/`load_Pallete` НЕ вызывают `fifoLifo_Inverse` и не учитывают `fifoLifo` - асимметрия LOAD/UNLOAD относительно FIFO/LIFO.

**Unknowns**

- Семантика ожидания `distance[3] >= 800` и констант 900/600/800/150 в доставке - из source не устанавливается.
- Почему WARN_PALLET_NOT_FOUND используется для «нет места впереди» (`Cntrl_V2/Cntrl_V2.ino:L5477-L5485`) - семантическое несоответствие кода и причины (суждение).
- Поведение waitTime-цикла зависит от момента последней записи глобального `count` (обновляется в `motor_Stop`/предыдущих pace-циклах, не обновляется в цикле ожидания `distance[3] >= 800` `Cntrl_V2/Cntrl_V2.ino:L5561-L5574`) - фактическая длительность паузы ≤ waitTime; точное значение в runtime не определяется статически.

**Disposition proposals (только предложения)**

- Preserve: последовательность «подвод -> поиск назад -> board-delay -> подъём -> recapture -> доставка к началу -> placement -> опускание»; observable behavior сохраняется.
- Preserve: профильные dst 600/670/500, recapture 100/250/450, maxbb-формулы, -25, формулы доводки с *0.96.
- Change: timeout `2000000/maxSpeed` - валидация/защита от maxSpeed==0 (`Cntrl_V2/Cntrl_V2.ino:L5453`); деление на ноль при невалидном конфиге CFG_MAX_SPEED (запись без проверки, `Cntrl_V2/Cntrl_V2.ino:L2955-L2961`).
- Change: цикл ожидания `distance[3] >= 800` без таймаута (`Cntrl_V2/Cntrl_V2.ino:L5561-L5574`) - риск неограниченного зависания на сенсоре.
- Change: отсутствие guard'а перед финальным `moove_Forward` в dispatch CMD_UNLOAD (`Cntrl_V2/Cntrl_V2.ino:L3279-L3280`) в отличие от LONG_UNLOAD (`Cntrl_V2/Cntrl_V2.ino:L3367-L3368`) - после abort операция продолжает движение.
- Change: асимметрия `fifoLifo_Inverse` (пересчёт позиции только при выходе из inverse, константа 800) и отсутствие FIFO/LIFO-обработки в LOAD - решение по целевой семантике за продуктом.
- Preserve/unknown: расширенный стоп-паттерн motor_Stop для UNLOAD с грузом (`Cntrl_V2/Cntrl_V2.ino:L2305-L2316`) - назначение не документировано, менять только с проверкой на железе.
- Exclude (предложение): мёртвые переменные `diffP` в stop_Before_Pallete_F/R (`Cntrl_V2/Cntrl_V2.ino:L4329`, `Cntrl_V2/Cntrl_V2.ino:L4362`, `Cntrl_V2/Cntrl_V2.ino:L4561`, `Cntrl_V2/Cntrl_V2.ino:L4592`) и закомментированные профильные блоки (`Cntrl_V2/Cntrl_V2.ino:L5429-L5431`, `Cntrl_V2/Cntrl_V2.ino:L5728`, `Cntrl_V2/Cntrl_V2.ino:L5772-L5774`).

---

#### Явно зафиксированные аномалии (сводно)

1. Деление на ноль: `2000000 / maxSpeed` при maxSpeed==0 (`Cntrl_V2/Cntrl_V2.ino:L5453`, `Cntrl_V2/Cntrl_V2.ino:L5802`); CFG_MAX_SPEED и MSG_CONFIG_SYNC_PUSH пишут значение без валидации (`Cntrl_V2/Cntrl_V2.ino:L2955-L2961`, `Cntrl_V2/Cntrl_V2.ino:L3051-L3052`).
2. uint8_t-заворот maxbb при maxSpeed > 170 (unload) / > 180 (load): `2/3 + (150 - maxSpeed)/10` отрицательно -> до 253 итераций по 100 мс (~25 с) (`Cntrl_V2/Cntrl_V2.ino:L5392`, `Cntrl_V2/Cntrl_V2.ino:L5734`).
3. Обратный поиск `single_Load` без таймаута: при тихом отказе `motor_Start_Reverse` (ToF gate, `Cntrl_V2/Cntrl_V2.ino:L2252-L2255`) цикл `Cntrl_V2/Cntrl_V2.ino:L5961-L5979` не выходит без внешнего stop/fault.
4. Тихий отказ `motor_Start_Forward/Reverse`: `motorStart` не выставляется, вызывающий код не проверяет результат (`Cntrl_V2/Cntrl_V2.ino:L2239-L2258`).
5. Ordering: dispatch CMD_UNLOAD перезаписывает status=CMD_MOVE_RIGHT_MAN после abort внутри `unload_Pallete`; у LONG_UNLOAD guard есть (`Cntrl_V2/Cntrl_V2.ino:L3279-L3280` vs `Cntrl_V2/Cntrl_V2.ino:L3367-L3368`).
6. Мёртвый код: `diffP` (4 anchor'а выше), закомментированные профильные ветки, `// if (shuttleLength == 800) pltMaxLn -= 20; ...`.
7. WARN_PALLET_NOT_FOUND для ситуации «нет места впереди» (`Cntrl_V2/Cntrl_V2.ino:L5477-L5485`) - семантическое несоответствие (суждение).
8. Повторная проверка `canAcceptCommandNow` в `SystemYield` после уже отправленного ACK_OK (`Cntrl_V2/Cntrl_V2.ino:L2871` vs `Cntrl_V2/Cntrl_V2.ino:L6977`, `Cntrl_V2/Cntrl_V2.ino:L7008`) - команда может быть отброшена после подтверждения (суждение о риске, факт - двойная проверка).
9. Асимметрия fifoLifo_Inverse (`Cntrl_V2/Cntrl_V2.ino:L4037-L4048`) и отсутствие FIFO/LIFO в LOAD.
10. `motor_Stop` UNLOAD-ветвь с 10x100 мс паузами без прямого `SystemYield` (`Cntrl_V2/Cntrl_V2.ino:L2307-L2315`).

---

## Группа: LongLoad / LongUnload / LongUnloadQuantity

Источник: `C:/Projects/Shuttle/ShuttleController`, evidence SHA `708d090980155d4a8d4644f7bcf87c383e81cd1d`.
Проверено: HEAD `4a226e5` отличается от evidence SHA только `docs/Controller-Nonblocking-Refactoring-Plan.md`; `git diff <evidence>..HEAD -- Cntrl_V2/` пуст (0 строк). Все anchors ниже сняты с working tree, совпадающей с evidence SHA по исходникам.
Файлы: `Cntrl_V2/Cntrl_V2.ino` (далее `.ino`), `Cntrl_V2/ShuttleProtocol.h` (далее `.h`).
Не использовались как evidence: `docs/Controller-Nonblocking-Refactoring-Plan.md`, `tests/`, refactor-ветки.

### Общая геометрия и соглашения (проверено по source)

- `distance[8]` (`.ino:594`): slot 0 = ToF ID1 channel reverse, slot 1 = ToF ID2 channel forward, slot 2 = ToF ID3 pallet reverse, slot 3 = ToF ID4 pallet forward. Маппинг подтверждён `updateTofDistance` (`.ino:3796-3808`) и `tofNameForSensor` (`.ino:3573-3585`). При `inverse==1` пары (0<->1) и (2<->3) меняются местами (`.ino:3798-3805`).
- `currentPosition`: 0 у начала канала (forward-конец); `moove_Forward` при останове у начала присваивает `currentPosition = 60` (`.ino:5254`), `moove_Before_Pallete_F` - `currentPosition = 60` / `distance[1] - 30` (`.ino:4494-4495`, `.ino:4508`).
- `motor_Speed` инвертирует знак CAN-скорости при `motorReverse ^ inverse` (`.ino:2113-2114`, `.ino:2144-2145`): при `inverse==1` «forward» физически едет в противоположный конец канала.
- `detect_Pallete` при `inverse==1` меняет местами GPIO-датчики F/R (`.ino:3408-3435`), с debounce `delay(5)` (`.ino:3414` и далее).
- `shouldAbortLoop()` (`.ino:8598-8601`), `preserveManualStopOnAbort()` (`.ino:8585-8591`), `isErrorActive()` (`.ino:8593-8596`) - установлены, здесь только применяются.
- CMD_STOP = 0x00 (`.h:87`). Поэтому `status = 0` и `status = CMD_STOP` семантически идентичны; `isShuttleIdle()` = `status == 0 || status == CMD_STOP` (`.ino:8424-8427`).
- Объявления команд: `CMD_LONG_LOAD = 0x22`, `CMD_LONG_UNLOAD = 0x23`, `CMD_LONG_UNLOAD_QTY = 0x24 // Requires MSG_CMD_WITH_ARG` (`.h:108-110`); `MSG_CMD_SIMPLE = 0x30`, `MSG_CMD_WITH_ARG = 0x31` (`.h:61-62`); `ParamCmdPacket {int32_t arg; uint8_t cmdType}` (`.h:367-371`); telemetry-состояния `STATE_LONG_LOAD=10`, `STATE_LONG_UNLOAD=11`, `STATE_LONG_UNLOAD_QTY=12` (`.h:157-159`).
- Warnings: `WARN_PALLET_NOT_FOUND=(1<<0)`, `WARN_CHANNEL_FULL=(1<<1)`, `WARN_NOT_IN_CHANNEL=(1<<2)`, `WARN_PALLET_SIZE_ERROR=(1<<3)`, `WARN_END_OF_CHANNEL=(1<<4)`, `WARN_OBSTACLE_AHEAD=(1<<7)` (`.h:186-196`). `setWarning` -> `alertMan.setWarning(warn, timeoutMs, millis())` (`.ino:550-553`); `setFault` -> `alertMan.setFault` (`.ino:545-548`).

---

#### LongLoad (CMD_LONG_LOAD = 0x22)

**Entry points и call sites**

- Поддерживается: `isSupportedCommand` включает `CMD_LONG_LOAD` (`.ino:8371`); проверка при парсинге `.ino:2782-2786` (иначе ACK_REJECTED `.ino:2784`).
- IDLE-ветка loop(): приём команды -> `currentOperation = mapCmdToOperation(status)` (`.ino:1843`, маппинг `CMD_LONG_LOAD -> STATE_LONG_LOAD` `.ino:8237-8238`), `lastPalletePosition = 0` (`.ino:1845`), `send_Cmd()` (`.ino:1846`), `currentMode = CoreOpMode::AUTO_EXEC` (`.ino:1870`).
- AUTO_EXEC: `run_Cmd()` (`.ino:1898-1900`); после: `if (status != CMD_STOP) status = 0; currentOperation = STATE_IDLE; currentMode = IDLE;` (`.ino:1902-1908`).
- MANUAL-режим тоже диспетчеризует `CMD_LONG_LOAD` с `currentOperation = STATE_LONG_LOAD` (`.ino:2001-2005`).
- Dispatch в `run_Cmd` (`.ino:3349-3358`): `send_Cmd(); lifter_Down(); long_Load(); if (status != CMD_STOP) moove_Forward(); status = CMD_STOP; send_Cmd();`.
- Функция `long_Load()` `.ino:6235-6363` (объявление `.ino:265`).
- Прямо/косвенно вызываемые физические функции: `moove_Forward` (`.ino:5197-5265`), `lifter_Down` (`.ino:2453-2519`), `lifter_Up` (`.ino:2361-2452`), `load_Pallete` (`.ino:5662-5939`), `moove_Distance_R` (`.ino:5004-5195`), `moove_Distance_F` (`.ino:4803-4996`), `moove_Before_Pallete_F` (`.ino:4410-4512`), `stop_Before_Pallete_R` (`.ino:4518-4638`), `moove_Before_Pallete_R` (`.ino:4640-4795`), `motor_Start_Forward/Reverse` (`.ino:2239-2259`), `motor_Speed` (`.ino:2088-2237`), `motor_Stop` (`.ino:2261-2330`), `detect_Pallete` (`.ino:3406-3455`), `get_Distance` (`.ino:3857-3919`), `set_Position` (`.ino:3922-4034`), `blink_Work` (`.ino:4051-4133`), `blink_Warning` (`.ino:4134+`), `SystemYield` (`.ino:6839-7058`).

**Admission/preconditions**

- Последовательность в парсере команд (`.ino:2777-2882`): `isSupportedCommand` (`.ino:2782`) -> provisioning: `!isProvisionedShuttle() && !isUnprovisionedCommandAllowed(reqCmd)` -> ACK_BAD_ENVIRONMENT (`.ino:2818-2827`; разрешены непривязанным только STOP/STOP_MANUAL/SYSTEM_RESET/RESET_ERROR, `.ino:8405-8408`) -> `isErrorActive() && !isOverrideCommand` -> ACK_ERROR_STATE (`.ino:2832-2841`) -> `!inChannel && !isOutOfChannelExemptCommand` -> WARN_NOT_IN_CHANNEL + ACK_BAD_ENVIRONMENT (`.ino:2843-2853`; exempt-список `.ino:8411-8417`: STOP/STOP_MANUAL/SYSTEM_RESET/SAVE_EEPROM/GET_CONFIG/RESET_ERROR/lift) -> `canAcceptCommandNow` (`.ino:2855`).
- `canAcceptCommandNow` (`.ino:8430-8461`): для CMD_LONG_LOAD ни одна специальная ветка не срабатывает -> fallback `return isShuttleIdle();` (`.ino:8460`); т.е. требуется `status == 0 || CMD_STOP` (`.ino:8424-8427`). Занятость -> ACK_BUSY (`.ino:2881`).
- Дополнительный inChannel-контроль в loop() при старте: тройное чтение CHANNEL с `delay(5)` (`.ino:1828-1833`), отказ -> WARN_NOT_IN_CHANNEL + `status = 0` (`.ino:1835-1840`).
- Аргументов у команды нет (MSG_CMD_SIMPLE).
- Физический гейт перед стартом моторов: `ensureChannelTofReadyForMotion` в `motor_Start_Forward/Reverse` (`.ino:2241`, `.ino:2252`, определение `.ino:3536-3553`): нет свежего измерения канального ToF -> `latchTofMeasurementFault` (fault -> abort через shouldAbortLoop).
- При принятии команды `lastPalletePosition = 0` (`.ino:1845`) - сброс признака «канал полон» перед стартом.

**Шаги и переходы (normal path)**

1. ACK_OK отправляется парсером до исполнения (`.ino:2871`); telemetry-state override для радио `predictTelemetryStateForAcceptedCommand` (`.ino:8271-8291`).
2. Dispatch: `send_Cmd()` (telemetry в SerialDisplay, `.ino:3400-3403`), `lifter_Down()` (`.ino:3351-3352`).
3. `long_Load()`: лог "Starting continuos load...", `status = CMD_LONG_LOAD`, `moove_Forward()` - выезд к началу канала (`.ino:6237-6239`); abort-проверка (`.ino:6240-6241`).
4. `status = CMD_LONG_LOAD; lifter_Down(); get_Distance(); detect_Pallete();` (`.ino:6242-6245`).
5. Если передние pallet-датчики не видят паллету `!(detectPalleteF1 && detectPalleteF2)` (`.ino:6246`): задний ход speed 10 с поиском до `detectPalleteF1 && detectPalleteF2` или `distance[1] >= 400 + chnlOffset` (`.ino:6248-6268`).
6. Если паллета так и не найдена (`.ino:6270`): `blink_Warning()`, WARN_PALLET_NOT_FOUND 5000 ms (`.ino:6272-6273`), отъезд назад `moove_Distance_R(shuttleLength + 100, 60, 30)` (`.ino:6275`), затем **бессрочный** цикл ожидания паллеты (`.ino:6278-6305`): выход при `(detectPalleteF1 && detectPalleteF2) || distance[3] < 1000` (`.ino:6289`), после чего 10000 ms стабилизации (`.ino:6292-6302`). Таймаута ожидания нет.
7. Если задние датчики видят паллету - `diffPallete = 10` (`.ino:6307-6308`).
8. Основной цикл `while (1)` (`.ino:6310`): `status = CMD_LONG_LOAD; load_Pallete();` (`.ino:6312-6313`).
9. Проверка «канал полон»: `if (lastPalletePosition && lastPalletePosition < shuttleLength * 2)` -> `blink_Warning(); setWarning(WARN_CHANNEL_FULL, 5000); status = 0; return;` (`.ino:6314-6320`). Это единственное штатное условие завершения.
10. Если шаттл у начала канала `distance[1] < 90 + chnlOffset && !isErrorActive()`: отъезд назад `moove_Distance_R(shuttleLength + 300, 60, 30)`, `wait = 1` (`.ino:6321-6329`); иначе `else if (shouldAbortLoop()) return;` (`.ino:6330-6331`).
11. Цикл ожидания следующей паллеты (`.ino:6335-6361`): **бессрочный**, выход при `(detectPalleteF1 && detectPalleteF2) || distance[3] < 1000` (`.ino:6346`) + 10000 ms стабилизации (`.ino:6349-6358`); затем повтор шага 8.
12. `load_Pallete()` (`.ino:5662-5939`): предварительная проверка «канал не забит» `lastPalletePosition && lastPalletePosition < shuttleLength * 2` -> WARN_CHANNEL_FULL + `status = CMD_STOP` + return (`.ino:5671-5677`); подъезд к паллете (`moove_Before_Pallete_F` при `distance[3] > 750`, `.ino:5688-5691`), заезд под паллет по передним доскам (`dst = 600`, 670 для 1200, `.ino:5720-5730`), ожидание под доской maxbb×100 ms (`.ino:5732-5768`), контроль длины `pltMaxLn = shuttleLength - 20` (`.ino:5771-5784`); `lifter_Up()` (`.ino:5859`); перехват при потере задних датчиков: назад `dist = 100/250/450` по профилю, затем вперёд до захвата (`.ino:5862-5922`); перевозка к месту укладки `stop_Before_Pallete_R()` (`.ino:5924`); т.к. `longWork == 0` (см. раздел longWork): `lifter_Down(); lastPallete = 1; lastPalletePosition = currentPosition;` (`.ino:5928-5933`); `loadCounter++` (`.ino:5938`).
13. Завершение по каналу-полному: `status = 0` (=CMD_STOP) -> в dispatch `if (status != CMD_STOP)` ложен -> **выезд вперёд не выполняется**, шаттл остаётся в месте последней укладки; `status = CMD_STOP; send_Cmd();` (`.ino:3354-3357`).

**Stop/fault/abort**

- `shouldAbortLoop()` проверяется: после moove_Forward (`.ino:6240`), в поисковом заднем ходе (`.ino:6253-6258`, принудительно `status = CMD_STOP`), в обоих wait-циклах (`.ino:6281-6285`, `.ino:6295-6300`, `.ino:6338-6342`, `.ino:6352-6356` - `preserveManualStopOnAbort()`), после load_Pallete через `else if (shouldAbortLoop()) return;` (`.ino:6330-6331`).
- CMD_STOP_MANUAL сохраняется `preserveManualStopOnAbort()` (`.ino:8585-8591`), но dispatch затем делает `if (status != CMD_STOP) moove_Forward();` (`.ino:3354`): при CMD_STOP_MANUAL вызывается moove_Forward, который сразу прерывается внутри (`.ino:5216-5221`), после чего `status = CMD_STOP` (`.ino:3356`) **перезаписывает CMD_STOP_MANUAL**.
- Локальные fault-пути load_Pallete, завершающие long_Load через shouldAbortLoop: WARN_PALLET_SIZE_ERROR + `status = CMD_STOP` (`.ino:5780-5783`); WARN_OBSTACLE_AHEAD + `status = CMD_STOP` при занятом месте у начала канала (`.ino:5818-5822`) и при `distance[3] < 500 && distance[3] < distance[1]` (`.ino:5844-5849`); потеря паллеты в перехвате у конца канала -> `status = CMD_STOP` (`.ino:5887-5892`).
- Локальный путь, НЕ завершающий цикл: таймаут поиска досок `millis() - cnt > 2000000 / maxSpeed || distance[1] < 80` -> WARN_OBSTACLE_AHEAD **без изменения status** (`.ino:5802-5809`); long_Load после этого либо отъезжает назад и ждёт (`.ino:6323-6328`), либо сразу повторяет load_Pallete (`.ino:6332-6333`).
- Fault lifter'а: `lifter_Down`/`lifter_Up` по таймауту `lifterDelay` ставят FAULT_LIFTER_TIMEOUT и `status = CMD_STOP` (`.ino:2501-2506`, `.ino:2412-2417`).
- FAULT_MOVE_TIMEOUT при застревании в `moove_Distance_R/F`: >5000 ms без движения (`.ino:5182-5189`, `.ino:4982-4989`); сокращённый выход 3000 ms при `lifterUp && dist < 30` (`.ino:5177-5181`).
- ToF-safety: `enforceActiveMotionTofSafety` ставит fault и `motor_Force_Stop()` при невалидном канальном ToF во время движения (`.ino:3555-3571`); вызывается из `get_Distance` (`.ino:3876`, `.ino:3917`).
- `SystemYield()` вызывается во всех длительных циклах long_Load (`.ino:6252`, `.ino:6280`, `.ino:6294`, `.ino:6337`, `.ino:6351`) и внутри всех физических подфункций; через него принимаются STOP-команды во время операции (`.ino:6977`, `.ino:7008` - `canAcceptCommandNow`, только override-команды проходят при занятом status).
- Любой active fault -> `currentMode = CoreOpMode::ERROR` на следующей итерации loop() (`.ino:1819-1820`) (cross-cutting).

**Observable outcomes**

- Штатное завершение (канал полон): WARN_CHANNEL_FULL (5000 ms), `status` в итоге CMD_STOP, `currentOperation` STATE_IDLE (`.ino:1906-1907`), шаттл остаётся в канале у последней укладки, финальный `send_Cmd()` (`.ino:3357`).
- Ожидание паллеты: бессрочное, WARN_PALLET_NOT_FOUND выставляется только при первичном ненахождении (`.ino:6273`); в wait-циклах предупреждение не повторяется.
- Прерывание: status CMD_STOP (или перезаписанный CMD_STOP_MANUAL -> CMD_STOP), финальная telemetry через `send_Cmd()` (`.ino:3357`).
- Счётчики: `sramStats->payload.loadCounter++` за каждую уложенную паллету (`.ino:5938`), `totalDist` через set_Position (`.ino:4017`, `.ino:4023`); пакет StatsPacket содержит loadCounter (`.ino:8929`).
- Telemetry в процессе: `send_Cmd()` только на входе/выходе dispatch (`.ino:3351`, `.ino:3357`); в цикле периодической рассылки состояния нет (в отличие от QTY-варианта). `shuttleStatus` в telemetry = `currentOperation` = STATE_LONG_LOAD (`.ino:8830`).
- `palleteCount` в long_Load НЕ изменяется (обновляется только в `pallete_Counting_F`, `.ino:6007`, `.ino:6066`, `.ino:6131`); telemetry несёт прежнее значение (`.ino:8834`).
- `firstPalletePosition`: в long_Load не записывается; сбрасывается в 0 только dispatch'ами CMD_DEMO/CMD_COMPACT_R (`.ino:3311`, `.ino:3343`); устанавливается операциями уплотнения (`.ino:6229`, `.ino:6796`); читается косвенно через `moove_Before_Pallete_F` (`.ino:4438-4443`, скоростной профиль при lifterUp) - т.е. long_Load потребляет результат ранее выполненного уплотнения.
- Логи: "Starting continuos load..." (`.ino:6237`), "Start loading pallete..." (`.ino:5664`), "End of channel, stop moove forward..." (`.ino:5256`), ошибки "Pallete error..." (`.ino:5804`), "Pallete error in BB... PLenght = %d" (`.ino:5778`).

**Timing conditions**

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| 50 ms | `.ino:5224`, `.ino:5240` | configured | период обновления позиции/скорости в moove_Forward |
| 50 ms | `.ino:2090-2092` | configured | троттлинг записи скорости в CAN (countMoove) |
| 50 ms | `.ino:6262` | configured | период set_Position/motor_Speed в поисковом заднем ходе |
| 400 + chnlOffset мм | `.ino:6250` | configured | граница поиска паллеты задним ходом от начала канала |
| shuttleLength + 100 мм | `.ino:6275` | configured | отъезд назад при ожидании паллеты (speed 60/30) |
| shuttleLength + 300 мм | `.ino:6326` | configured | отъезд назад от начала канала между паллетами (speed 60/30) |
| 10000 ms | `.ino:6292`, `.ino:6349` | configured | стабилизация после детекции паллеты перед load |
| без таймаута | `.ino:6278-6305`, `.ino:6335-6361` | configured (отсутствие) | бессрочное ожидание следующей паллеты |
| 100 ms × maxbb | `.ino:5746-5754` | configured | выдержка «доезда под доску» в load_Pallete |
| 2000000/maxSpeed ms (~20833 при maxSpeed=96) | `.ino:5802` | inferred (формула в source) | таймаут поиска досок -> WARN_OBSTACLE_AHEAD |
| distance[3] < 1000 мм | `.ino:6289`, `.ino:6346` | configured | признак «паллета появилась» (pallet-forward ToF) |
| distance[1] <= 90 + chnlOffset мм | `.ino:5246` | configured | конец канала (начало) для moove_Forward |
| distance[1] > 80 мм | `.ino:5248-5249` | configured | creep speed=5 для LOAD/LONG_LOAD вместо остановки |
| distance[1] < 450 + chnlOffset и distance[3] > 400 мм | `.ino:5682-5683` | configured | признание «шаттл в начале канала с паллетой сверху и местом впереди» |
| distance[2] <= interPalleteDistance + 600 мм | `.ino:5816` | configured | занято позади -> WARN_OBSTACLE_AHEAD |
| lastPalletePosition < shuttleLength*2 | `.ino:5671`, `.ino:6314` | configured | канал полон |
| waitTime 15000 ms (clamp 5000-30000) | `.ino:585`, `.ino:7653-7656` | configured | в long_Load не используется (только unload) |
| 3800 ms lifterDelay | `.ino:1646`, `.ino:2501`, `.ino:2412` | configured | таймаут лифтера -> FAULT_LIFTER_TIMEOUT |
| 5000 ms / 3000 ms | `.ino:5182-5189` / `.ino:5177-5181` | configured | stall в moove_Distance_* -> FAULT_MOVE_TIMEOUT |
| 5000 ms | все setWarning в scope | configured | авто-истечение warning |

**Resource effects**

- CAN: скорость движения ID 100 (`motor_Speed`, `.ino:2096`), лифтер ID 101 (`.ino:2374`, `.ino:2463`), чтение/очистка RX в blink_Work (`.ino:4057`) и motor-функциях (`.ino:2296-2297` и др.).
- I2C: ToF-опрос round-robin 4 сенсоров из get_Distance (`.ino:3857-3919`, не чаще раза в 8 ms на сенсор `.ino:3861-3862`, с quiet-guard относительно BMS TX `.ino:3864-3869`); AS5600 в set_Position через `readAs5600AngleForMotion` (`.ino:3923-3929`).
- GPIO: pallet-датчики DATCHIK_F1/F2/R1/R2 (`.ino:3406-3455`), CHANNEL (`.ino:1828-1833`, `.ino:2830`), концевики DL_UP/DL_DOWN (`.ino:2363`, `.ino:2455`), LED в blink_Work/blink_Warning.
- UART: `send_Cmd()` -> `sendTelemetryPacket(&SerialDisplay)` (`.ino:3400-3403`) на входе/выходе операции; приём команд на обоих UART через SystemYield (cross-cutting).
- Blocking delay: `delay(5)` в detect_Pallete (`.ino:3414-3434`), `delay(5)` в перехвате (`.ino:5902`), `delay(10)` в циклах лифтера (`.ino:2508`); все короткие, без SystemYield.
- Flash/EEPROM: логи makeLog; статистика loadCounter в sramStats (`.ino:5938`), сохранение по pendingEepromSave (`.ino:1811-1815`).

**Профильные варианты 800/1000/1200 (в load-пути)**

- `dst` заезда под паллету: 600 мм базово, 670 при shuttleLength==1200 (`.ino:5724-5726`); 800-специфичная правка закомментирована (`.ino:5728`).
- maxbb (число выдержек под доской): `3 + (150 - maxSpeed) / 10`, +3 при distance[1] < 300, -3 при 1200 (`.ino:5734-5744`).
- Контроль длины паллеты: `pltMaxLn = shuttleLength - 20` (`.ino:5771`); профильные вычеты закомментированы (`.ino:5772-5774`).
- Перехват: dist 100/250/450 мм для 800/1000/1200 (`.ino:5864-5869`).
- stop_Before_Pallete_R: `dist -= 25` при shuttleLength 1000/1200 (`.ino:4627-4628`).
- shuttleLength конфигурируется CFG_SHUTTLE_LEN (`.h:124` подразумевается; запись `.ino:2951-2952`, EEPROM `.ino:7594`), значение по умолчанию 1000 (`.ino:598`); клампа значения в source нет (в отличие от waitTime).

**Unknowns**

- Физический смысл `distance[3] < 1000` как «паллета подана погрузчиком» - установлено только по контексту wait-циклов; кто и как кладёт паллету, source не фиксирует.
- Реальная вместимость канала при условии `lastPalletePosition < shuttleLength * 2` зависит от channelLength и размеров паллет; из source выводится только эвристика.
- Длительность полного цикла не устанавливается (зависит от длины канала, числа паллет, waitTime-подобных ожиданий нет).

**Disposition proposals (только предложения)**

- Preserve: бессрочное ожидание паллеты вместо завершения по «паллета не найдена» - это заявленный инвариант каталога V3; подтверждён source (`.ino:6278-6305`, `.ino:6335-6361`).
- Preserve: завершение только по заполнению канала (`lastPalletePosition < shuttleLength * 2`) - единственный штатный выход (`.ino:6314-6320`).
- Change: `status = 0` как «канал полон» (`.ino:6318`) неотличим от CMD_STOP (CMD_STOP=0x00, `.h:87`) - в V3 нужен различимый результат завершения.
- Change: WARN_OBSTACLE_AHEAD без смены status (`.ino:5802-5809`) даёт неограниченные повторы load_Pallete без эскалации - предложить счётчик повторов/fault.
- Change: CMD_STOP_MANUAL перезаписывается CMD_STOP в dispatch (`.ino:3354-3356`) - различие manual stop / stop теряется.
- Change: отсутствие периодической telemetry внутри цикла long_Load (только вход/выход, `.ino:3351`, `.ino:3357`) - при долгом ожидании паллеты оператор видит только STATE_LONG_LOAD.
- Exclude: закомментированные профильные ветки (`.ino:5728`, `.ino:5772-5774`) - мёртвый код.
- Unknown: нужно ли в V3 сохранять creep `speed = 5` до distance[1] <= 80 (`.ino:5248-5249`) - поведение прижатия к стене.

---

#### LongUnload (CMD_LONG_UNLOAD = 0x23)

**Entry points и call sites**

- `isSupportedCommand` включает CMD_LONG_UNLOAD (`.ino:8372`); парсинг `.ino:2782-2786`.
- IDLE-ветка: `mapCmdToOperation`: CMD_LONG_UNLOAD -> STATE_LONG_UNLOAD (`.ino:8239-8240`); `lastPalletePosition = 0` (`.ino:1845`); `currentMode = AUTO_EXEC` (`.ino:1870`); MANUAL-ветка также диспетчеризует (`.ino:2006-2010`).
- Dispatch в `run_Cmd` (`.ino:3359-3373`):
  1. `send_Cmd();` (`.ino:3361`)
  2. `longWork = 1;` (`.ino:3362`)
  3. `lifter_Down();` (`.ino:3363`)
  4. `moove_Forward();` (`.ino:3364`) - выезд к началу канала
  5. `long_Unload();` (`.ino:3365`)
  6. `longWork = 0;` (`.ino:3366`)
  7. Ранний выход: `if ((status == CMD_STOP && distance[1] > 100) || isErrorActive()) return;` (`.ino:3367-3368`) - без выездного движения и без финального send_Cmd
  8. Иначе `status = CMD_MOVE_RIGHT_MAN; moove_Forward(); status = CMD_STOP; send_Cmd();` (`.ino:3369-3372`) - выезд к началу канала.
- Функция `long_Unload()` без аргумента `.ino:6366-6454` (объявление `.ino:266`).
- Косвенно: `unload_Pallete` (`.ino:5329-5659`), `moove_Before_Pallete_R` (`.ino:4640-4795`), `stop_Before_Pallete_F` (`.ino:4292-4408`), `moove_Before_Pallete_F` (`.ino:4410-4512`), `moove_Distance_R/F`, `lifter_Up/Down`, `fifoLifo_Inverse` (`.ino:4037-4049`), motor-функции, `SystemYield`.

**Admission/preconditions**

- Идентичны LongLoad (isSupportedCommand, provisioning, error-state, inChannel, `canAcceptCommandNow` -> `isShuttleIdle()` `.ino:8460`); аргументов нет.
- Принятие: ACK_OK (`.ino:2871`), при занятости ACK_BUSY (`.ino:2881`).
- `lastPalletePosition = 0` при принятии (`.ino:1845`).

**Шаги и переходы (normal path)**

1. `long_Unload()`: сохраняет `oldInterPalleteDistance`, ставит `interPalleteDistance = 700` (`.ino:6368-6369`) - временный override межпаллетной дистанции на время операции (восстановление `.ino:6451`).
2. Если `fifoLifo` - `fifoLifo_Inverse()` (`.ino:6371-6372`): инверсия направления/сенсоров (см. раздел FIFO/LIFO).
3. `moove_Forward()` к началу канала (`.ino:6373`); abort -> восстановление inverse/interPalleteDistance + return (`.ino:6374-6380`).
4. Цикл `while (detect)` (`.ino:6381-6450`):
   - если fifoLifo - инверсия вокруг `unload_Pallete()` (`.ino:6384-6388`);
   - `unload_Pallete()` (`.ino:6386`) - снимает одну паллету и вывозит её к началу канала (см. ниже);
   - **условие завершения «канал пуст»**: `if (distance[0] < 200 + chnlOffset && !isErrorActive()) { detect = 0; status = CMD_LONG_UNLOAD; break; }` (`.ino:6389-6394`) - шаттл упёрся в задний конец канала;
   - abort: восстановление inverse/interPalleteDistance + return (`.ino:6395-6401`);
   - ожидание освобождения зоны впереди: `while (distance[3] < 900 && distance[1] > 700)` с SystemYield/abort (`.ino:6402-6417`);
   - подъезд к началу канала: `if (distance[1] > 700)` цикл `while (distance[1] > 90)` со ступенчатой скоростью 20 / distance[1]/20 / 6 (`.ino:6418-6446`);
   - `motor_Stop(); lifter_Down(); moove_Distance_R(shuttleLength + 500, 80, 80);` (`.ino:6447-6449`) - опустить лифтер (паллета, оставленная поднятой из-за longWork, опускается здесь) и отъехать назад к следующей паллете.
5. `unload_Pallete()` (`.ino:5329-5659`), ключевые этапы:
   - входная инверсия при fifoLifo (`.ino:5332-5333`); `lifter_Down()` (`.ino:5339`); `startDiff = 20` при `distance[0] < 90 + chnlOffset` (`.ino:5341-5342`);
   - подъезд к паллете `moove_Before_Pallete_R()` при `distance[2] > 750` (`.ino:5345-5349`);
   - поиск досок задним ходом (`.ino:5357-5465`): передняя доска -> фиксированный проезд `dst` 600/670/500 (`.ino:5378-5389`), задняя доска -> выдержки maxbb×100 ms (`.ino:5390-5426`);
   - контроль длины `pltMaxLn = shuttleLength - 20` -> WARN_PALLET_SIZE_ERROR + `moove_Forward()` + `status = CMD_STOP` (`.ino:5428-5441`);
   - таймаут поиска `2000000/maxSpeed || distance[0] < 80` -> WARN_OBSTACLE_AHEAD **без смены status** (`.ino:5453-5462`);
   - после поиска: `if (distance[3] < 900)` -> нет места впереди: `lifter_Down(); WARN_PALLET_NOT_FOUND; moove_Forward(); status = CMD_STOP; return;` (`.ino:5476-5485`);
   - `lifter_Up()` (`.ino:5486`); `pstn = currentPosition` при `distance[2] < 600` (`.ino:5487-5489`);
   - перехват при потере передних датчиков: вперёд `dist` 100/250/450, назад до захвата или до конца канала (там `channelLength = currentPosition + shuttleLength + distance[0] - 30`, `status = CMD_STOP`, `endOfChannel = 1`) (`.ino:5491-5554`, конец канала `.ino:5518-5525`);
   - для статусов CMD_UNLOAD/CMD_LONG_UNLOAD/CMD_LONG_UNLOAD_QTY (`.ino:5555`): вывоз к началу канала: `moove_Before_Pallete_F()` (`.ino:5557`), при `distance[1] > 150` - ожидание свободы `while (distance[3] < 800)` (`.ino:5561-5572`), ожидание `waitTime` (`.ino:5575-5590`), движение вперёд до `distance[1] <= 90 + chnlOffset` со скоростью 20/ступени, останов при `distance[3] < 600 && distance[1] > distance[3]` (`.ino:5592-5625`), `currentPosition = distance[1] - 30` (`.ino:5627`);
   - **из-за longWork==1 опускание паллеты пропускается**: `if (!longWork && lifterUp) lifter_Down();` (`.ino:5642-5643`) - паллета остаётся на поднятом лифтере;
   - bookkeeping: `lastPalletePosition = pstn + 800|1000 + interPalleteDistance` (`.ino:5644-5647`), `lastPallete = pstn ? 1 : 0` (`.ino:5648-5651`), `unloadCounter++` (`.ino:5656`).

**Stop/fault/abort**

- shouldAbortLoop в long_Unload: `.ino:6374`, `.ino:6395`, `.ino:6406-6413`, `.ino:6424-6431` (два последних принудительно ставят `status = CMD_STOP` и восстанавливают inverse/interPalleteDistance).
- shouldAbortLoop внутри unload_Pallete: `.ino:5350-5356`, `.ino:5365-5373` (ставит CMD_STOP), `.ino:5411-5416` (preserveManualStopOnAbort), `.ino:5505-5513`, `.ino:5563-5571`, `.ino:5579-5588`, `.ino:5594-5602`, `.ino:5635-5639`.
- Ранний return dispatch (`.ino:3367-3368`): `status == CMD_STOP && distance[1] > 100` (прерывание в глубине канала) или любой active fault -> без выездного moove_Forward и без финального send_Cmd; управление возвращается в AUTO_EXEC, где `status != CMD_STOP` сбрасывается в 0 (`.ino:1902-1904`).
- Особый случай: прерывание с сохранённым CMD_STOP_MANUAL (через preserveManualStopOnAbort, напр. `.ino:6395-6401`) НЕ попадает в ранний return (`status == CMD_STOP` ложен) -> выполняется выездной `moove_Forward()` (`.ino:3370`), который сразу прерывается внутри (`.ino:5216-5221`), и `status = CMD_STOP` (`.ino:3371`) перезаписывает CMD_STOP_MANUAL.
- Специальное поведение `motor_Stop` для UNLOAD/LONG_UNLOAD при поднятом лифтере и паллете впереди (`distance[3] + 100 < distance[1]`): укороченная рампа `delay(10)` (`.ino:2288-2289`) и 10 повторных записей нулевой скорости с шагом 100 ms (`.ino:2305-2316`).
- **Утечка `inverse` при fifoLifo==1**: два abort-пути unload_Pallete возвращают управление БЕЗ восстановления fifoLifo: maxbb-выдержка (`.ino:5410-5415`, preserveManualStopOnAbort + motor_Stop + return) и WARN_PALLET_SIZE_ERROR (`.ino:5432-5441`, moove_Forward + status=CMD_STOP + return), в отличие от всех остальных abort-путей (`.ino:5352-5354`, `.ino:5370-5372`, `.ino:5459-5460`, `.ino:5509-5511`, `.ino:5567-5569`, `.ino:5583-5585`, `.ino:5597-5599`, `.ino:5637-5638`). Трассировка при fifoLifo==1: unload_Pallete входит с inverse=1 (через свой toggle `.ino:5332-5333` поверх toggle long_Unload); после возврата без toggle long_Unload делает toggle `.ino:6387-6388` (inverse->0), затем abort-ветка `.ino:6397-6398` делает ещё один toggle (inverse->1) и возвращает управление с inverse=1 вместо 0. Последующие команды движения исполняются физически инвертированно до следующего сбалансированного fifoLifo-цикла или CFG_REVERSE_MODE. Дополнительно в пути WARN_PALLET_SIZE_ERROR `moove_Forward()` (`.ino:5438`) вызывается до `status = CMD_STOP` (`.ino:5439`) и при inverse=1 физически едет к заднему концу канала.
- Lifter/ToF/move fault'ы - как в LongLoad (FAULT_LIFTER_TIMEOUT `.ino:2412-2417`, `.ino:2501-2506`; FAULT_MOVE_TIMEOUT `.ino:5182-5188`; enforceActiveMotionTofSafety `.ino:3555-3571`).
- `waitTime`-ожидание (`.ino:5575`) проверяет shouldAbortLoop (`.ino:5579-5588`).

**Observable outcomes**

- Штатное завершение (канал пуст): последняя итерация завершается WARN_OBSTACLE_AHEAD + LOG_ERROR "Pallete error..." из финального безрезультатного поиска досок (`.ino:5453-5462`) - это нормальный путь выхода, затем `distance[0] < 200 + chnlOffset` -> break (`.ino:6389-6394`); status=CMD_LONG_UNLOAD -> выезд `moove_Forward()` (`.ino:3370`) -> шаттл у начала канала; `status = CMD_STOP; send_Cmd();` (`.ino:3371-3372`).
- Прерывание в глубине канала: ранний return, статус CMD_STOP/0, шаттл остаётся на месте; warning/fault по причине.
- Счётчики: `unloadCounter++` за каждую снятую паллету (`.ino:5656`); StatsPacket (`.ino:8930`).
- Telemetry: `send_Cmd()` только вход/выход dispatch (`.ino:3361`, `.ino:3372`); внутри цикла long_Unload() периодической рассылки нет; `currentOperation` = STATE_LONG_UNLOAD (`.ino:8830`).
- `interPalleteDistance` временно 700 (`.ino:6369`) - влияет на расчёт дистанции укладки в `stop_Before_Pallete_F/R` (`dist = dist/4 - interPalleteDistance - 100 - diff - mprOffset`, `.ino:4396`, `.ino:4626`).
- `endOfChannel` может быть установлен в перехвате unload (`.ino:5524`), в moove_Reverse (`.ino:5316`), в moove_Before_Pallete_R (`.ino:4791`); в long_Unload читается опосредованно через скоростной профиль moove_Before_Pallete_R (`.ino:4729-4733`).
- `palleteCount` не изменяется.
- Логи: "Starting continuos unload..." (`.ino:6370`), "Start unloading pallete..." (`.ino:5331`), "Pallete lenght = %d" (`.ino:5467`), "Last pallete position after unload = %d" (`.ino:5654`).

**Timing conditions**

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| interPalleteDistance = 700 мм | `.ino:6369`, `.ino:6451` | configured | override на время long unload |
| distance[0] < 200 + chnlOffset мм | `.ino:6389` | configured | канал пуст (задний конец) -> завершение |
| shuttleLength + 500 мм | `.ino:6449` | configured | отъезд назад к следующей паллете (speed 80/80) |
| distance[3] < 900 мм && distance[1] > 700 мм | `.ino:6403` | configured | ожидание освобождения зоны впереди |
| distance[1] > 700 мм | `.ino:6418` | configured | порог начала подъезда к началу канала |
| distance[1] > 90 мм | `.ino:6421` | configured | цикл подъезда к началу канала |
| speed 20 / distance[1]/20 / 6 | `.ino:6437-6442` | configured | ступени скорости подъезда |
| distance[2] > 750 мм | `.ino:5345` | configured | нужен подъезд к паллете перед поиском |
| dst 600/670/500 мм | `.ino:5382-5386` | configured | фиксированный проезд после передней доски |
| 100 ms × maxbb | `.ino:5403-5424` | configured | выдержки под доской |
| 2000000/maxSpeed ms (~20.8 s при 96) | `.ino:5453` | inferred (формула) | таймаут поиска досок |
| distance[0] < 80 мм | `.ino:5453` | configured | упор в задний конец при поиске |
| distance[3] < 900 мм | `.ino:5477` | configured | нет места куда везти -> WARN_PALLET_NOT_FOUND + CMD_STOP |
| distance[2] < 600 мм | `.ino:5488` | configured | фиксация pstn для lastPalletePosition |
| distance[3] < 800 мм | `.ino:5561` | configured | ожидание свободной зоны перед вывозом |
| waitTime 15000 ms (5000-30000) | `.ino:5575`, `.ino:585`, `.ino:7653-7656` | configured | пауза перед вывозом паллеты к началу |
| distance[3] < 600 мм && distance[1] > distance[3] | `.ino:5613-5614` | configured | стоп при сближении с препятствием впереди |
| distance[1] > 100 мм | `.ino:3367` | configured | порог раннего return dispatch |

**Resource effects**

- Как в LongLoad; дополнительно: CAN-записи нулевой скорости 10×100 ms в motor_Stop при выезде с паллетой (`.ino:2305-2316`); `delay(10)` в рампе motor_Stop (`.ino:2289`).

**Профильные варианты 800/1000/1200 (в unload-пути)**

- dst: 600 базово; 670 при 1200; **500 при 800 у конца канала** (`channelLength - currentPosition - shuttleLength < 1500 && shuttleLength == 800`) (`.ino:5382-5386`) - активно, в отличие от load-пути.
- maxbb: `2 + (150 - maxSpeed) / 10`, +3 при distance[0] < 300, -3 при 1200 (`.ino:5392-5402`) - база 2 против 3 в load.
- pltMaxLn = shuttleLength - 20 (`.ino:5428`), профильные вычеты закомментированы (`.ino:5429-5431`).
- Перехват: 100/250/450 мм (`.ino:5493-5497`).
- stop_Before_Pallete_F: `dist -= 25` при 1000/1200 (`.ino:4397-4398`).

**Unknowns**

- Физическая логика `while (distance[3] < 900 && distance[1] > 700)` (`.ino:6403`): точный сценарий (паллета ещё не забрана конвейером/погрузчиком при отъезде шаттла) устанавливается только по контексту.
- Почему опускание паллеты перенесено из unload_Pallete в long_Unload (`.ino:5642-5643` vs `.ino:6448`) - из source следует только механика longWork.

**Disposition proposals (только предложения)**

- Preserve: завершение по заднему концу канала `distance[0] < 200 + chnlOffset` (`.ino:6389`) - соответствует инварианту «останов при заполнении/конце канала, не при отсутствии паллеты».
- Change: WARN_OBSTACLE_AHEAD + LOG_ERROR как побочный эффект штатного завершения (`.ino:5453-5462`) - в V3 различать «паллет больше нет» и «препятствие».
- Change: `distance[1] > 100` в раннем return без chnlOffset (`.ino:3367`) - несогласованность с остальными порогами (`90 + chnlOffset` и др.).
- Change: CMD_STOP_MANUAL теряется (перезапись в `.ino:3371`; ранний return его не распознаёт `.ino:3367`).
- Change: ранний return не отправляет финальный send_Cmd/telemetry (`.ino:3367-3368`).
- Preserve: временный interPalleteDistance=700 (`.ino:6369`) - документированное поведение разреживания при выгрузке.
- Unknown: необходимость 10× повторных CAN-записей нуля в motor_Stop (`.ino:2305-2316`) - требует подтверждения на железе.

---

#### LongUnloadQuantity (CMD_LONG_UNLOAD_QTY = 0x24)

**Entry points и call sites**

- `isSupportedCommand` включает CMD_LONG_UNLOAD_QTY (`.ino:8373`); объявление `.h:110` с пометкой `Requires MSG_CMD_WITH_ARG`.
- Парсинг аргумента: `if (realMsgID == MSG_CMD_WITH_ARG) { ... else if (reqCmd == CMD_LONG_UNLOAD_QTY) UPQuant = (uint8_t)cmdArgs->arg; }` (`.ino:2857-2863`); `ParamCmdPacket {int32_t arg; uint8_t cmdType}` (`.h:367-371`) - arg усекается до uint8_t (0-255).
- IDLE-ветка: `mapCmdToOperation`: CMD_LONG_UNLOAD_QTY -> STATE_LONG_UNLOAD_QTY (`.ino:8241-8242`); `lastPalletePosition = 0` (`.ino:1845`); AUTO_EXEC (`.ino:1870`).
- **В MANUAL-режиме ветки для CMD_LONG_UNLOAD_QTY нет** (в отличие от CMD_LONG_LOAD/CMD_LONG_UNLOAD, `.ino:2001-2010`); в MANUAL `canAcceptCommandNow` для неё возвращает `isShuttleIdle()`, что ложно при status==CMD_MANUAL_MODE -> ACK_BUSY (`.ino:8460`, `.ino:2881`).
- Dispatch в `run_Cmd` (`.ino:3374-3390`): как у LongUnload, плюс:
  - `status = CMD_LONG_UNLOAD_QTY;` перед вызовом (`.ino:3380`) - **no-op**: status уже равен CMD_LONG_UNLOAD_QTY (по нему и произошёл вход в ветку);
  - `long_Unload(UPQuant);` (`.ino:3381`);
  - `UPQuant = 0;` после (`.ino:3382`).
- Функция `long_Unload(uint8_t num)` `.ino:6457-6556` (объявление `.ino:267`).

**Admission/preconditions**

- Как у LongUnload; дополнительно: команда обязана прийти в MSG_CMD_WITH_ARG, иначе `UPQuant` сохранит значение от предыдущей команды. Проверки типа сообщения на сам reqCmd нет: если CMD_LONG_UNLOAD_QTY придёт в MSG_CMD_SIMPLE, парсер не запишет аргумент (`.ino:2857`), и будет использован старый/нулевой UPQuant.
- qty=0: `UPQuant = 0` -> цикл `while (detect && num)` (`.ino:6473`) не выполняется ни разу; функция восстанавливает interPalleteDistance/inverse и возвращается (`.ino:6553-6555`); dispatch затем выполняет выездной `moove_Forward()` (статус != CMD_STOP).
- qty больше числа паллет в канале: цикл завершается по условию «канал пуст» `distance[0] < 200 + chnlOffset` (`.ino:6484-6489`); остаток `num` отбрасывается, UPQuant сбрасывается в 0 в dispatch (`.ino:3382`).

**Шаги и переходы (normal path)**

1. `interPalleteDistance = 700` (`.ino:6459-6460`), fifoLifo-инверсия (`.ino:6462-6463`), `moove_Forward()` (`.ino:6464`), abort-обработка (`.ino:6465-6471`).
2. Цикл `while (detect && num)` (`.ino:6473`):
   - `status = CMD_LONG_UNLOAD_QTY; send_Cmd();` (`.ino:6475-6476`) - периодическая telemetry в Display на каждой итерации (отличие от безаргументного варианта);
   - инверсия вокруг `unload_Pallete()` (`.ino:6477-6481`);
   - `get_Distance(); blink_Work();` (`.ino:6482-6483`);
   - «канал пуст»: `distance[0] < 200 + chnlOffset && !isErrorActive()` -> `detect = 0; status = CMD_LONG_UNLOAD_QTY; break;` (`.ino:6484-6489`);
   - abort -> восстановление + return (`.ino:6490-6496`);
   - `status = CMD_LONG_UNLOAD_QTY; send_Cmd();` (`.ino:6497-6498`);
   - ожидание/подъезд как в long_Unload (`.ino:6499-6542`);
   - `motor_Stop(); lifter_Down(); num--; UPQuant--;` (`.ino:6544-6547`) - декремент и локального num, и глобального UPQuant;
   - `status = CMD_LONG_UNLOAD_QTY; send_Cmd();` (`.ino:6548-6549`);
   - `if (num) moove_Distance_R(shuttleLength + 500, 80, 80);` (`.ino:6550-6551`) - отъезд к следующей паллете только если остались паллеты в задании (последняя итерация не отъезжает).
3. Восстановление interPalleteDistance/inverse (`.ino:6553-6555`).
4. Dispatch: `UPQuant = 0; longWork = 0;` (`.ino:3382-3383`); ранний return `(status == CMD_STOP && distance[1] > 100) || isErrorActive()` (`.ino:3384-3385`); иначе выезд `status = CMD_MOVE_RIGHT_MAN; moove_Forward(); status = CMD_STOP; send_Cmd();` (`.ino:3386-3389`).

**Stop/fault/abort**

- Идентично LongUnload: shouldAbortLoop в циклах (`.ino:6465`, `.ino:6490`, `.ino:6503-6510`, `.ino:6520-6527` - последние два ставят CMD_STOP), внутри unload_Pallete те же пути; lifter/ToF/move fault'ы те же.
- Отличие: при прерывании `num`/`UPQuant` не восстанавливаются, но UPQuant всё равно сбрасывается в dispatch (`.ino:3382`).

**Observable outcomes**

- Завершение по qty: после `num` успешных снятий цикл выходит без отъезда (`.ino:6550-6551`), статус CMD_LONG_UNLOAD_QTY -> выезд вперёд (`.ino:3386-3387`) -> CMD_STOP + send_Cmd. Шаттл у начала канала.
- Завершение по пустому каналу раньше qty: WARN_OBSTACLE_AHEAD из финального поиска (как в LongUnload) + break (`.ino:6484-6489`).
- Telemetry: три send_Cmd на итерацию (`.ino:6476`, `.ino:6498`, `.ino:6549`) - единственный long-вариант с внутрицикловой рассылкой.
- UPQuant: читается только в dispatch (`.ino:3381`) и декрементируется в цикле (`.ino:6547`); **в telemetry/статистике не отражается** - декремент не наблюдаем (мёртвый bookkeeping).
- unloadCounter++ за каждую снятую паллету (`.ino:5656`); currentOperation = STATE_LONG_UNLOAD_QTY в telemetry (`.ino:8830`).

**Timing conditions**

- Таблица LongUnload полностью (общие подфункции) + `num` итераций; дополнительных временных констант у QTY-варианта нет.

**Resource effects**

- Как LongUnload + дополнительные записи telemetry в SerialDisplay на каждой итерации (`.ino:6476`, `.ino:6498`, `.ino:6549`).

**Профильные варианты**

- Идентичны LongUnload (общая unload_Pallete).

**Unknowns**

- Ожидаемое поведение при qty=0 (штатный «нулевой» прогон с выездом или ошибка аргумента) в source не специфицировано - фактически выполняется холостой прогон с выездом.
- Нет валидации верхней границы arg (int32_t -> uint8_t, `.ino:2863`): значения >255 усечения; интерпретация 256 как 0 не документирована.

**Disposition proposals (только предложения)**

- Preserve: семантика «выгрузить N паллет или до конца канала, что раньше» (`.ino:6473`, `.ino:6484-6489`).
- Change: отсутствие валидации/нормализации аргумента (0, >числа паллет, усечение int32->uint8, `.ino:2863`) - в V3 явные правила для qty=0 и qty>доступно.
- Change: `UPQuant--` не наблюдаем (`.ino:6547`, нет в telemetry `.ino:8820-8840`) - либо убрать, либо выставлять оставшееся количество в telemetry.
- Change: no-op `status = CMD_LONG_UNLOAD_QTY;` в dispatch (`.ino:3380`).
- Change: CMD_LONG_UNLOAD_QTY недоступен из MANUAL-режима (нет ветки `.ino:1991-2010`, ACK_BUSY через `.ino:8460`) - зафиксировать решение: сохранить как ограничение или добавить ветку.
- Change: MSG_CMD_SIMPLE с CMD_LONG_UNLOAD_QTY принимает старый UPQuant (`.ino:2857-2863`) - валидация типа сообщения.

---

#### Флаг longWork

**Факты**

- Объявление: `uint8_t longWork = 0; // Флаг продолжительной загрузки/выгрузки` (`.ino:608`).
- Записи: только в dispatch: `longWork = 1` / `longWork = 0` вокруг `long_Unload()` (`.ino:3362`, `.ino:3366`) и вокруг `long_Unload(UPQuant)` (`.ino:3377`, `.ino:3383`).
- **CMD_LONG_LOAD longWork НЕ устанавливает** (dispatch `.ino:3349-3358` не трогает флаг) - асимметрия между long load и long unload.
- Чтения: ровно два:
  - `unload_Pallete`: `if (!longWork && lifterUp) lifter_Down();` (`.ino:5642-5643`) - при longWork==1 паллета остаётся поднятой после вывоза к началу канала; опускание происходит в long_Unload (`.ino:6448`) / long_Unload(num) (`.ino:6545`);
  - `load_Pallete`: `if (!longWork && lifterUp) { lifter_Down(); lastPallete = 1; lastPalletePosition = currentPosition; }` (`.ino:5928-5933`) - при longWork==1 укладка не фиксирует lastPallete/lastPalletePosition (недостижимо в текущем коде, т.к. load_Pallete вызывается с longWork==1 только гипотетически: CMD_LONG_LOAD флаг не ставит, других вызовов load_Pallete при longWork==1 нет).
- Влияние на telemetry/safety/timing: отсутствует (других чтений нет).
- Вывод: фактическая роль longWork в V1 - отложить опускание лифта из unload_Pallete в long_Unload; ветка load_Pallete с longWork==1 - мёртвая логика (недостижима при текущем dispatch).

**Disposition proposals**

- Change: асимметрия (unload ставит флаг, load нет) и мёртвая ветка в load_Pallete (`.ino:5928-5933`) - в V3 либо явный контракт «кто опускает лифт в long-цикле», либо удаление флага.

---

#### FIFO/LIFO

**Факты**

- `uint8_t fifoLifo = 0; // Режим FIFO/LIFO -сохранять-` (`.ino:611`); конфигурируется CFG_FIFO_LIFO=9 (`.h:132`, запись `.ino:2990-2996`, полная конфигурация `.ino:3063-3064`, ответ GET_CONFIG `.ino:3089`, `.ino:3149`, EEPROM save/load `.ino:7593`, `.ino:7627`).
- `fifoLifo_Inverse()` (`.ino:4037-4049`): переключает глобальный `inverse`; при inverse 1->0 делает `currentPosition = channelLength - currentPosition - 800` (`.ino:4040-4043`); при 0->1 позиция не пересчитывается (асимметрия).
- Эффекты `inverse`:
  - перестановка ToF-дистанций 0<->1, 2<->3 (`.ino:3798-3805`);
  - перестановка pallet-GPIO F<->R в detect_Pallete (`.ino:3408-3413`);
  - инверсия знака CAN-скорости: `motorReverse ^ inverse` (`.ino:2113-2114`, `.ino:2144-2145`) - «forward» физически разворачивается;
  - set_Position использует ветвление `(motorReverse == 0) ^ inverse` (`.ino:3930`).
- В long-операциях: при fifoLifo==1 инверсия включается один раз перед стартом и выключается после (long_Load не использует fifoLifo вообще - в long_Load вызовов fifoLifo_Inverse нет), и дополнительно toggles вокруг каждого unload_Pallete: long_Unload `.ino:6371-6372`, `.ino:6384-6388`, `.ino:6452-6453`; long_Unload(num) `.ino:6462-6463`, `.ino:6477-6481`, `.ino:6554-6555`; внутри unload_Pallete вход/выход `.ino:5332-5333`, `.ino:5657-5658` и на abort-путях `.ino:5352-5354`, `.ino:5370-5372`, `.ino:5459-5460`, `.ino:5509-5511`, `.ino:5567-5569`, `.ino:5583-5585`, `.ino:5597-5599`, `.ino:5637-5638`.
- Итог: при fifoLifo==1 long unload физически работает с противоположным концом канала (все сенсоры и направление движения инвертированы); порядок снятия паллет относительно канала меняется на LIFO.
- Отдельный флаг `inverse` конфигурируется CFG_REVERSE_MODE=10 (`.h:133`, `.ino:2997-3004`) - независимо от fifoLifo; при смене значения также делает `currentPosition = channelLength - currentPosition - 800` (`.ino:2998-3001`).
- `endOfChannel` (`.ino:583`): устанавливается при достижении заднего конца в moove_Reverse (`.ino:5314-5316`), moove_Before_Pallete_R (`.ino:4790-4791`), moove_Distance_R (`.ino:5148-5152` - только channelLength без endOfChannel), в перехвате unload_Pallete (`.ino:5518-5525`); читается в скоростном профиле moove_Before_Pallete_R (`.ino:4729-4733`). Сброса endOfChannel в long-операциях нет (grep: записи только в перечисленных местах).

**Disposition proposals**

- Preserve: поддержка FIFO/LIFO как направление long-операций (подтверждено механизмом inverse).
- Change: двойной конфигурационный флаг (fifoLifo + inverse) с одним физическим эффектом и асимметричный пересчёт currentPosition (`.ino:4037-4049` vs отсутствие при 0->1) - унифицировать в V3.
- Unknown: взаимодействие fifoLifo и long_Load - в V1 long_Load инверсию не использует (вызовов нет); является ли это intended (LIFO-load через reverse-конфигурацию шаттла) - из source не устанавливается.

---

#### Условия остановки циклов (ключевой инвариант)

Факты (проверено по source):

1. **LongLoad не завершается от отсутствия паллеты.** Оба wait-цикла (`.ino:6278-6305`, `.ino:6335-6361`) бессрочные: выход только по появлению паллеты (`detectPalleteF1 && detectPalleteF2 || distance[3] < 1000`) или по shouldAbortLoop. WARN_PALLET_NOT_FOUND выставляется один раз при первичном ненахождении (`.ino:6273`) и не является условием выхода.
2. **LongLoad завершается штатно только по «канал полон»**: `lastPalletePosition && lastPalletePosition < shuttleLength * 2` -> WARN_CHANNEL_FULL, return (`.ino:6314-6320`; дублирующий pre-check в load_Pallete `.ino:5671-5677` ставит CMD_STOP). Других штатных выходов из `while(1)` (`.ino:6310`) нет - в частности, нет выхода по «конец канала» как отдельному условию: конец канала (distance[1] <= 90+chnlOffset) обрабатывается как позиция у начала с отъездом назад (`.ino:6323-6328`) или creep'ом в moove_Forward (`.ino:5246-5257`).
3. **LongUnload/Qty завершаются штатно по «канал пуст» = шаттл у заднего конца**: `distance[0] < 200 + chnlOffset && !isErrorActive()` (`.ino:6389-6394`, `.ino:6484-6489`); Qty дополнительно по исчерпанию `num` (`.ino:6473`, `.ino:6550-6551`). Отдельного условия «паллета не найдена» как штатного завершения нет: отсутствие паллет проявляется через упор в конец канала при поиске.
4. **Общие выходы для всех трёх**: CMD_STOP / CMD_STOP_MANUAL / active fault через shouldAbortLoop (`.ino:8598-8601`) - проверяется во всех длительных циклах; локальные load/unload fault'ы со сменой status на CMD_STOP: WARN_PALLET_SIZE_ERROR (`.ino:5780-5783`, `.ino:5437-5440`), WARN_PALLET_NOT_FOUND-нет-места (`.ino:5483`), WARN_OBSTACLE_AHEAD при занятом месте (`.ino:5818-5822`, `.ino:5843-5849`), конец канала в перехвате (`.ino:5887-5892`, `.ino:5518-5525`).
5. **Исключение из инварианта**: WARN_OBSTACLE_AHEAD по таймауту поиска досок НЕ меняет status (`.ino:5453-5462`, `.ino:5802-5809`) - long_Load после этого повторяет load_Pallete (риск неограниченного цикла повторов); long_Unload после него обычно попадает на условие конца канала (`.ino:6389`).
6. Инвариант каталога V3 «long-операции НЕ завершаются от отсутствия паллеты; останов только при заполнении канала, stop/manual stop, fault/error или локальном load/unload fault» - **подтверждён** с уточнением: для long unload «заполнение канала» заменяется на «конец канала (канал пуст)», а локальный fault WARN_OBSTACLE_AHEAD по таймауту поиска не завершает операцию (повтор).

#### Явно зафиксированные мёртвый код / ordering-баги

1. `status = CMD_LONG_UNLOAD_QTY;` в dispatch QTY (`.ino:3380`) - no-op (status уже таков).
2. `status = 0` при канале-полном (`.ino:6318`) неотличим от CMD_STOP (CMD_STOP=0x00, `.h:87`) - различие не работает; выезд вперёд в dispatch (`.ino:3354`) не выполняется ни при status=0, ни при CMD_STOP.
3. Мёртвая ветка longWork в load_Pallete (`.ino:5928-5933`): load_Pallete никогда не вызывается при longWork==1 (CMD_LONG_LOAD флаг не ставит, `.ino:3349-3358`).
4. UPQuant декрементируется (`.ino:6547`), но нигде не читается кроме dispatch (`.ino:3381`) - не наблюдаем.
5. Закомментированные профильные ветки: `.ino:5429-5431`, `.ino:5728`, `.ino:5772-5774`.
6. CMD_STOP_MANUAL перезаписывается CMD_STOP в обоих dispatch (`.ino:3356`, `.ino:3371`, `.ino:3388`), включая путь через preserveManualStopOnAbort.
7. Ручной stop при long unload с сохранённым CMD_STOP_MANUAL не распознаётся ранним return (`.ino:3367`) -> попытка выездного moove_Forward с немедленным самопрерыванием.
8. WARN_OBSTACLE_AHEAD без смены status (`.ino:5453-5462`, `.ino:5802-5809`) - retry без эскалации/счётчика.
9. `moove_Forward` в dispatch LONG_LOAD при CMD_STOP_MANUAL (`.ino:3354-3355`) стартует мотор (motor_Start_Forward, `.ino:5200`) до первой проверки shouldAbortLoop внутри цикла (`.ino:5216`) - короткое неконтролируемое включение привода.
10. CMD_LONG_UNLOAD_QTY отсутствует в MANUAL-диспетчере (`.ino:1991-2010`) при наличии там LONG_LOAD/LONG_UNLOAD - асимметрия покрытия.
11. endOfChannel не сбрасывается при старте long-операций (записи только в `.ino:4791`, `.ino:5316`, `.ino:5524`; сбросов нет).
12. Утечка `inverse=1` после abort'а в unload_Pallete путями `.ino:5410-5415` и `.ino:5432-5441` при fifoLifo==1 (нет fifoLifo_Inverse перед return) - все последующие движения физически инвертированы (трассировка в разделе Stop/fault LongUnload).
13. В пути WARN_PALLET_SIZE_ERROR unload_Pallete движение `moove_Forward()` (`.ino:5438`) выполняется до установки `status = CMD_STOP` (`.ino:5439`) - полный проезд к концу канала после ошибки размера паллеты.

---

## Группа: CompactPallets (F/R), CountPallets, Demo

Источник: `C:/Projects/Shuttle/ShuttleController` (локальное зеркало). Evidence SHA: `708d090980155d4a8d4644f7bcf87c383e81cd1d`. HEAD `4a226e5` отличается только `docs/Controller-Nonblocking-Refactoring-Plan.md` (не используется). Все anchors прочитаны и сверены с working tree.

Общие координаты файлов: `Cntrl_V2/Cntrl_V2.ino`, `Cntrl_V2/ShuttleProtocol.h`.

---

### 0. Общий контекст группы (единый для трёх операций)

#### 0.1 Объявления команд

- `CMD_DEMO = 0x06`: `Cntrl_V2/ShuttleProtocol.h:L93` (блок "Lifecycle & State").
- `CMD_COMPACT_F = 0x25`, `CMD_COMPACT_R = 0x26`, `CMD_COUNT_PALLETS = 0x27`: `Cntrl_V2/ShuttleProtocol.h:L111-L113` (блок "Auto Operations").

#### 0.2 Supported-список

- `isSupportedCommand()`: `Cntrl_V2/Cntrl_V2.ino:L8351-L8384`. В switch присутствуют `CMD_DEMO` (L8360), `CMD_COMPACT_F` (L8374), `CMD_COMPACT_R` (L8375), `CMD_COUNT_PALLETS` (L8376) - все три операции формально поддержаны.

#### 0.3 Путь приёма команды (admission, общий)

1. Приём кадра и парсинг - `pollSerial()` для display UART и radio UART, вызывается только из `SystemYield()`: `Cntrl_V2/Cntrl_V2.ino:L6950` (display), `L6954` (radio). Возврат команды через `processPacket()`: `L3215-L3221`.
2. `processPacket()`: `L2713-L2930`. Проверки для MSG_CMD_SIMPLE/MSG_CMD_WITH_ARG (`L2778`):
   - `isSupportedCommand(reqCmd)` - иначе `ACK_REJECTED` (`L2782-L2786`).
   - provisioning: `!isProvisionedShuttle() && !isUnprovisionedCommandAllowed(reqCmd)` - иначе `ACK_BAD_ENVIRONMENT` (`L2817-L2825`). `isUnprovisionedCommandAllowed` (`L8406-L8409`) разрешает только STOP/STOP_MANUAL/SYSTEM_RESET/RESET_ERROR - все три операции требуют provisioning.
   - active fault: `isErrorActive() && !isOverrideCommand(reqCmd)` - иначе `ACK_ERROR_STATE` (`L2828-L2837`). Наши команды не override (`isOverrideCommand` `L8418-L8423`).
   - in-channel: `bool inChannel = digitalRead(CHANNEL)` (`L2839`), `!inChannel && !isOutOfChannelExemptCommand(reqCmd)` - `setWarning(WARN_NOT_IN_CHANNEL, 5000)` + `ACK_BAD_ENVIRONMENT` (`L2841-L2850`). `isOutOfChannelExemptCommand` (`L8411-L8416`): наши команды НЕ освобождены.
   - занятость: `canAcceptCommandNow(reqCmd, replyPort == &SerialLora)` (`L2851`); для наших команд (не override/manual/lift/distance) - `return isShuttleIdle()` (`L8430-L8465`, финальный `return` на `L8465`), т.е. `status == 0 || status == CMD_STOP` (`isShuttleIdle` `L8425-L8428`). Иначе `ACK_BUSY` (`L2876-L2883`).
   - приём: `sendCommandAck(header->seq, ACK_OK, ...)` с `telemStateOverride` для LoRa (`L2866-L2872`), возврат `reqCmd`.
3. Присвоение `status` выполняется в `SystemYield()`: stop-команды приоритетны (`L6962-L6973`, сразу `motor_Stop()`); обычные команды - `status = cmdRad` / `status = cmdDisp` только при `canAcceptCommandNow` (`L6975-L7004` radio, `L7005-L7035` display). Факт: новая операция не может быть назначена во время выполнения другой (guard `isShuttleIdle`), stop проходит всегда.

#### 0.4 Запуск операции из loop()

- `loop()`: `Cntrl_V2/Cntrl_V2.ino:L1805`. Каждый проход: `SystemYield()` (`L1807`), `get_Distance()` (`L1817`), `if (isErrorActive()) currentMode = CoreOpMode::ERROR` (`L1819-L1820`).
- `case CoreOpMode::IDLE` (`L1824`): при `status != 0 && status != CMD_STOP` (`L1826`) - повторная проверка in-channel с тройным опросом `digitalRead(CHANNEL)` и `delay(5)` (`L1829-L1833`); при провале - `makeLog(LOG_WARN, "Command 0x%02X rejected: Shuttle not in channel", status)`, `setWarning(WARN_NOT_IN_CHANNEL, 5000)`, `status = 0` (`L1835-L1839`).
- При успехе: `currentOperation = mapCmdToOperation(status)` (`L1843`), лог "Shuttle accepted CMD" (`L1844`), `lastPalletePosition = 0` (`L1845`), `send_Cmd()` (`L1846`), `currentMode = CoreOpMode::AUTO_EXEC` (`L1870`, ветка не-manual).
- `mapCmdToOperation()`: `L8229-L8269`. `CMD_COMPACT_F/CMD_COMPACT_R -> STATE_COMPACT` (`L8243-L8245`), `CMD_DEMO -> STATE_DEMO` (`L8246-L8247`), `CMD_COUNT_PALLETS -> STATE_COUNT_PALLETS` (`L8248-L8249`). Значения состояний: `ShuttleProtocol.h:L145-L164` (`STATE_COMPACT = 4`, `STATE_DEMO = 6`, `STATE_COUNT_PALLETS = 7`).
- `case CoreOpMode::AUTO_EXEC` (`L1898-L1908`): `run_Cmd()` (`L1900`); после возврата `if (status != CMD_STOP) status = 0` (`L1902-L1905`); `currentOperation = STATE_IDLE; currentMode = CoreOpMode::IDLE` (`L1906-L1907`).
- Из `CoreOpMode::MANUAL` (`L1911`) через `run_Cmd()` диспетчеризуются только CMD_LOAD/UNLOAD/LONG_LOAD/LONG_UNLOAD (`L1990-L2010`); DEMO/COMPACT/COUNT в MANUAL-ветке отсутствуют, а приём в MANUAL блокируется `canAcceptCommandNow` (не idle) - недостижимы из manual-режима.

#### 0.5 Dispatch в run_Cmd() и мёртвые ветки (ordering-баг)

`run_Cmd()`: `Cntrl_V2/Cntrl_V2.ino:L3228`. Ветки группы:

- DEMO: `L3308-L3312` - `demo_Mode(); firstPalletePosition = 0;` - **нет ни одного `send_Cmd()` в ветке** (в отличие от всех соседних операций).
- COUNT_PALLETS: `L3313-L3324` - `pallete_Counting_F(); status = CMD_STOP; send_Cmd(); status = CMD_COUNT_PALLETS; makeLog(LOG_INFO, "Pallete count = %d", palleteCount); status = CMD_MOVE_RIGHT_MAN; moove_Forward(); status = CMD_STOP; send_Cmd();` - нет входного `send_Cmd()` перед функцией.
- COMPACT_F: `L3330-L3338`:

  ```cpp
  send_Cmd();                       // L3332
  pallete_Compacting_F();           // L3333
  status = CMD_STOP;                // L3334
  if (status != CMD_STOP)           // L3335
      moove_Forward();              // L3336  <- МЁРТВО
  send_Cmd();                       // L3337
  ```

- COMPACT_R: `L3339-L3348`:

  ```cpp
  send_Cmd();                       // L3341
  pallete_Compacting_R();           // L3342
  firstPalletePosition = 0;         // L3343
  status               = CMD_STOP;  // L3344
  if (status != CMD_STOP)           // L3345
      moove_Forward();              // L3346  <- МЁРТВО
  send_Cmd();                       // L3347
  ```

**Факт (ordering-баг/мёртвый код):** в обеих COMPACT-ветках `status = CMD_STOP` присваивается ДО проверки `if (status != CMD_STOP)` (L3334->L3335, L3344->L3345), поэтому `moove_Forward()` на L3336/L3346 недостижим. Сравнение с эталонным порядком: CMD_LONG_LOAD `L3349-L3358` - сначала `if (status != CMD_STOP) moove_Forward();` (L3354-L3355), затем `status = CMD_STOP;` (L3356); CMD_CALIBRATE `L3298-L3307` - тот же корректный порядок (L3303-L3305). В LONG_LOAD выход `moove_Forward()` жив, в COMPACT_F/R - мёртв. Суждение: вероятно транспозиция строк при рефакторинге; семантический смысл выхода "вперёд после уплотнения" в V1 фактически не выполняется.

Дополнительно: `firstPalletePosition = 0` на L3343 (COMPACT_R) - живой код: ранние `return` из `pallete_Compacting_R()` оставляют `firstPalletePosition` ненулевым, сброс делает dispatch. В COMPACT_F сброса нет, но `pallete_Compacting_F` его и не пишет.

#### 0.6 send_Cmd, ACK, телеметрия

- `send_Cmd()`: `L3400-L3403` - `sendTelemetryPacket(&SerialDisplay)`; это снапшот телеметрии в display-UART, а не радио-ACK. ACK-коды отправляются отдельно `sendCommandAck` (cross-cutting).
- `sendTelemetryPacket()`: `L8793`; `pkt->shuttleStatus = currentOperation` (`L8828`), `pkt->palleteCount = palleteCount` (`L8834`) - счётчик паллет и состояние операции наблюдаемы в каждом пакете телеметрии.

---

### 1. CompactPallets Forward (CMD_COMPACT_F = 0x25)

#### Entry points и call sites

- Объявление прототипа: `Cntrl_V2/Cntrl_V2.ino:L268` (`void pallete_Compacting_F();`).
- Определение: `Cntrl_V2/Cntrl_V2.ino:L6138-L6183`.
- Dispatch: `L3330-L3338` (см. 0.5). Единственный call site - `L3333`.
- Прямо вызываемые физические функции: `lifter_Down()` (L6142; определение `L2453-L2512`), `moove_Reverse()` (L6143; `L5268-L5327`), `get_Distance()` (L6146/L6161; `L3857-L3920`), `detect_Pallete()` (L6148; `L3406-L3466`), `moove_Distance_F(100, 25, 25)` (L6153; `L4803-L4996`), `motor_Speed()` (L6163/L6165; `L2088-L2238`), `motor_Stop()` (L6156/L6177; `L2261-L2329`), `load_Pallete()` (L6171; `L5662-L5939`), `blink_Work()` (L6170; `L4051-L4130`).
- Косвенно через `load_Pallete()`: `moove_Before_Pallete_F()` (`L4410-L4517`), `moove_Distance_F/R`, `lifter_Up()` (`L2361`), `stop_Before_Pallete_R()` (`L4518-L4638`) -> `moove_Before_Pallete_R()` (`L4640-L4795`), `set_Position()` (`L3922-L4036`), `setWarning()` (`L550-L553`), `blink_Warning()` (`L4134+`).

#### Admission/preconditions

- Полный путь 0.3: supported (L8374), provisioning, !error, in-channel (двойная проверка: processPacket L2839-L2850 и loop L1829-L1839), `isShuttleIdle()`. Аргументов у команды нет (MSG_CMD_SIMPLE).
- Внутри функции: lifter опускается только если `!digitalRead(DL_DOWN)` (L6141-L6142).

#### Шаги и переходы (normal path)

1. Лог "Start compacting pallete forward..." (L6140).
2. Lifter вниз при необходимости (L6141-L6142).
3. `moove_Reverse()` (L6143) - проезд к концу канала (reverse-торец, `distance[0]`-сторона); при `lifterUp` `moove_Reverse` вместо полного проезда делает `stop_Before_Pallete_R()` + `lifter_Down()` (L5272-L5277). Обновляет `channelLength` на торце (L5309-L5316).
4. `get_Distance()` (L6146), `status = CMD_COMPACT_F` (L6147), `detect_Pallete()` (L6148).
5. Цикл подхода (L6149-L6166): пока `distance[3] < 700` (паллетный forward-ToF < 700 мм) && сработал любой из 4 паллетных сенсоров && `distance[1] > 100 + chnlOffset` (не у торца): `SystemYield()` (L6152), шаг `moove_Distance_F(100, 25, 25)` (L6153) - 100 мм вперёд, затем торможение `motor_Speed(min(distance[1]/20, oldSpeed))` (L6162-L6165). Направление: вперёд, к паллетам от конца канала.
6. Цикл перестановки (L6167-L6182): пока `status != CMD_STOP`: `SystemYield()` (L6169), `blink_Work()` (L6170), `load_Pallete()` (L6171) - берёт паллет и ставит их глубже к концу канала (`stop_Before_Pallete_R`, L5924), `compactCounter++` (L6172), выход при `distance[1] < 150 && !lifterUp` (L6173-L6174) - шаттл дошёл до переднего торца, переставлять больше нечего; `status = CMD_COMPACT_F` (L6181) перезапись после каждой итерации.
7. Завершение в dispatch: `status = CMD_STOP`, `send_Cmd()` (L3334, L3337).

Условия завершения: естественное - L6173-L6174; выходы по abort - см. ниже. Цикл L6167 теоретически может быть покинут только через `return`: тело завершается `status = CMD_COMPACT_F` (L6181), поэтому условие `status != CMD_STOP` на следующей итерации всегда истинно.

#### Подоперация load_Pallete() (L5662-L5939), используемая Compact_F

1. Проверка "канал не забит": `lastPalletePosition && lastPalletePosition < shuttleLength * 2` -> `WARN_CHANNEL_FULL`, `status = CMD_STOP`, return (L5671-L5677).
2. Подход к паллету: если не в стартовой конфигурации - `moove_Before_Pallete_F()` при `distance[3] > 750` (L5687-L5689); движение вперёд к доскам со скоростью 20/oldSpeed (L5697-L5705).
3. Передняя доска: `moove_Distance_F(dst, oldSpeed, 10)`, `dst = 600` (670 при shuttleLength==1200) (L5722-L5729).
4. Дожим под доску: `maxbb = 3 + (150 - maxSpeed)/10` (+3 при distance[1]<300; -3 при 1200), серия задержек по 100 мс (L5733-L5766).
5. Ошибки: `WARN_PALLET_SIZE_ERROR` при `palleteLenght >= shuttleLength - 20` без задней доски (L5773-L5784); `WARN_OBSTACLE_AHEAD` по таймауту `millis() - cnt > 2000000 / maxSpeed` или `distance[1] < 80` (L5802-L5808); двойное штабелирование: паллетный сенсор + `distance[1] < 300 + chnlOffset` && `distance[2] <= interPalleteDistance + 600` -> `WARN_OBSTACLE_AHEAD`, `status = CMD_STOP` (L5814-L5821) - **локальное применение inter-pallet distance**.
6. Подъём `lifter_Up()` (L5856), перехват при необходимости: `moove_Distance_R(dist, 15, 10)`, dist = 100/250/450 для 800/1000/1200 (L5860-L5866).
7. Перевозка к концу канала: `stop_Before_Pallete_R()` (L5924); в `stop_Before_Pallete_R` дистанция постановки `dist = dist/4 + diffPallete - interPalleteDistance - 100 - diff - mprOffset` (L4626), `-25` при 1000/1200 (L4627), `dist *= 0.96` (L4629) - **основное применение inter-pallet distance в compact**.
8. `lifter_Down()` при `!longWork && lifterUp`, `lastPallete = 1`, `lastPalletePosition = currentPosition` (L5928-L5932), `loadCounter++` (L5938).

#### Stop/fault/abort

- `shouldAbortLoop()` (cross-cutting, L8598-L8601) проверяется: после `moove_Reverse` (L6144-L6145, return без смены status), в цикле подхода (L6154-L6159: `motor_Stop`, `status = CMD_STOP`, return), в цикле перестановки (L6175-L6180: то же).
- `SystemYield()` вызывается на каждой итерации обоих циклов (L6152, L6169) и внутри всех подфункций движения - stop-команда из UART принимается в любой момент; `motor_Stop()` при stop выполняется уже в SystemYield (L6972).
- Внутренние faults подфункций: `FAULT_MOTOR_STALL` из `blink_Work` (L4078-L4085: `motor_Stop`, `status = CMD_STOP`, `setFault`) - далее `shouldAbortLoop` завершает операцию; `FAULT_LIFTER_TIMEOUT` из `lifter_Down` (L2500-L2506); ToF-faults и safety - через `get_Distance`/SystemYield (cross-cutting). Любой active fault -> `isErrorActive` -> abort + `CoreOpMode::ERROR` в loop (L1819-L1820).
- Warnings, устанавливаемые на пути (не faults): WARN_CHANNEL_FULL (L5674), WARN_PALLET_SIZE_ERROR (L5780), WARN_OBSTACLE_AHEAD (L5807, L5820) - операция при этом завершается через `status = CMD_STOP`/abort, но не переходит в ERROR (warnings != faults).

#### Observable outcomes

- `sramStats->payload.compactCounter` инкрементируется после КАЖДОГО вызова `load_Pallete()`, включая неудачные (L6172) - факт; счётчик завышается при прерванных циклах. Поле `StatsPacket.compactCounter`: `ShuttleProtocol.h:L277`, выдача в stats-пакет `L8931`.
- `loadCounter++` на каждый успешный перенос (L5938, `ShuttleProtocol.h:L275`).
- Финал: `status = CMD_STOP` (dispatch L3334), `currentOperation = STATE_IDLE` (L1906), телеметрия `shuttleStatus = STATE_COMPACT` во время исполнения (L1843, L8828).
- Логи: "Start compacting pallete forward...", "Pallete error..."/"Pallete error in BB..." при ошибках, debug-логи stop_Before_Pallete_R.
- Позиция: `currentPosition` обновляется `set_Position()` по AS5600 (L3922); `channelLength` уточняется в `moove_Reverse` на торце (L5313).

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| Шаг подхода 100 мм, maxSpeed 25, minSpeed 25 | L6153 | configured | дискретный подъезд к штабелю |
| `distance[3] < 700` мм | L6149 | configured | условие подхода (паллетный forward-ToF) |
| `distance[1] > 100 + chnlOffset` | L6150 | configured | guard переднего торца в подходе |
| `distance[1] < 150 && !lifterUp` | L6173-L6174 | configured | условие завершения операции |
| `speed = distance[1]/20`, кап `oldSpeed` | L6162-L6165 | configured | торможение в подходе |
| `dist/4 + diffPallete - interPalleteDistance - 100 - diff - mprOffset`, x0.96 | L4626-L4629 | configured | дистанция постановки паллета |
| `dst = 600` (670 для 1200) | L5724-L5726 | configured | заезд под паллет по передней доске |
| `2000000 / maxSpeed` мс | L5802 | configured | таймаут поиска паллета -> WARN_OBSTACLE_AHEAD |
| 100 мс задержка дожима (maxbb) | L5743-L5766 | configured | дожид под доской |
| Троттлинг motor_Speed 50 мс | L2090 | configured | rate-limit CAN-команд скорости |
| blinkTime = 80 мс | L584, L4054 | configured | квант blink_Work (LED/CAN-drain/stall-детект) |
| Stall-детект 1500 мс | L4078 | configured | FAULT_MOTOR_STALL при пробуксовке |

#### Resource effects

- CAN: скорости/останов ID 100 (`motor_Speed` L2094, `motor_Stop` L2267), lifter ID 101 (`lifter_Down` L2464); `blink_Work` и `motor_Stop` дренируют CAN RX (L4057, L2265).
- I2C: ToF-опрос `get_Distance()` ротирует 4 сенсора, троттлинг >= 8 мс (L3861), пауза при BMS TX (L3864-L3869).
- GPIO: 4 паллетных сенсора с debounce `delay(5)` (`detect_Pallete` L3414-L3460), DL_DOWN/DL_UP endstops, CHANNEL, GREEN/WHITE LED (blink_Work), RED/ZOOMER (blink_Warning в подфункциях).
- UART: телеметрия display (send_Cmd, SystemYield 300 мс), приём команд только через SystemYield.
- Flash: нет записи (sramStats в RAM с CRC, L328-L334); логи в log-буфер через makeLog (L94-L100).
- Delays: `delay(5)` в detect_Pallete и перехватах (L5536, L5903 - в load/unload, но load_Pallete использует L5903).

#### Профильные варианты 800/1000/1200

- `dst` заезда под паллет: 600 (800/1000), 670 (1200) - L5724-L5726.
- `maxbb`: `-3` при 1200 (L5737-L5743) - меньше дожима.
- Перехват: dist 100/250/450 для 800/1000/1200 (L5862-L5866).
- `pltMaxLn = shuttleLength - 20` (L5769) - предел длины паллета растёт с профилем.
- Закомментированные профильные поправки `pltMaxLn -= 20/100/150` (L5770-L5772) - мёртвые комментарии.

#### Unknowns

- Физическая скорость 25/oldSpeed в единицах расстояния/времени из source не устанавливается (единицы `motor_Speed` - условные 0-100, L2100-L2102).
- Реакция на ситуацию "паллет нет вовсе" в цикле перестановки: `load_Pallete` в этом случае упирается в WARN_OBSTACLE_AHEAD/WARN_PALLET_SIZE_ERROR, но отдельного "empty channel" исхода у Compact_F нет - поведение при пустом канале определяется этими warning-путями.

#### Disposition proposals

- Мёртвый выход `moove_Forward()` после компакта (L3335-L3336): **change** - в V3 либо удалить, либо восстановить корректный порядок (как в LONG_LOAD), предварительно выяснив у заказчика, нужен ли выезд после компакта.
- Инкремент `compactCounter` до проверки результата load_Pallete (L6172): **change** - считать только успешные переносы.
- Semantics "уплотнение к концу канала через load_Pallete": **preserve** - функциональное ядро.
- inter-pallet distance в формуле постановки (L4626): **preserve**.
- Перезапись `status = CMD_COMPACT_F` после каждой итерации (L6160, L6181): **change** - в V3 lifecycle команды не должен перезаписываться исполнителем.

---

### 2. CompactPallets Reverse (CMD_COMPACT_R = 0x26)

#### Entry points и call sites

- Прототип: `L269`. Определение: `L6186-L6232`. Dispatch: `L3339-L3348`, call site `L3342`.
- Прямо вызываемые: `moove_Forward()` (L6189; `L5197-L5263`), `get_Distance()` (L6192/L6207), `detect_Pallete()` (L6194), `moove_Distance_R(100, 25, 25)` (L6199; `L5004-L5196`), `motor_Speed()` (L6209/L6211), `motor_Stop()` (L6202/L6224), `unload_Pallete()` (L6218; `L5329-L5660`), `blink_Work()` (L6217).
- Косвенно через `unload_Pallete()`: `fifoLifo_Inverse()` (`L4037-L4048`), `moove_Before_Pallete_R()` (L5347), `lifter_Up/Down`, `stop_Before_Pallete_F()` (L5633; `L4292-L4408`) -> `moove_Before_Pallete_F()` (L4410), `set_Position`, `setWarning`, `blink_Warning`.

#### Admission/preconditions

- Идентично Compact_F (0.3), supported на L8375. Аргументов нет.

#### Шаги и переходы (normal path)

1. Лог "Start compacting pallete reverse..." (L6188).
2. `moove_Forward()` (L6189) - проезд к переднему торцу (началу канала, `distance[1]`-сторона); при `lifterUp` - `stop_Before_Pallete_F()` + `lifter_Down()` (L5202-L5207).
3. `get_Distance()` (L6192), `status = CMD_COMPACT_R` (L6193), `detect_Pallete()` (L6194).
4. Цикл подхода (L6195-L6212): пока `distance[2] < 700` (паллетный reverse-ToF) && любой паллетный сенсор && `distance[0] > 100 + chnlOffset`: `SystemYield()` (L6198), шаг `moove_Distance_R(100, 25, 25)` (L6199) - назад, к штабелю; торможение `min(distance[0]/20, oldSpeed)` (L6208-L6211).
5. `status = CMD_COMPACT_R` (L6213). Цикл перестановки (L6214-L6230): `SystemYield()` (L6216), `blink_Work()` (L6217), `unload_Pallete()` (L6218) - берёт паллет и ставит их к началу канала (`stop_Before_Pallete_F`, L5633), `compactCounter++` (L6219), выход при `distance[0] < 150 && !lifterUp` (L6220-L6221), `status = CMD_COMPACT_R` (L6228), `firstPalletePosition = currentPosition` (L6229) - якорь для скоростного проезда в `moove_Before_Pallete_F` на следующей итерации (L4438-L4443).
6. После цикла `firstPalletePosition = 0` (L6231).

**Факт (мёртвый код):** L6230-L6231 недостижимы. Тело цикла завершается `status = CMD_COMPACT_R` (L6228), поэтому условие `while (status != CMD_STOP)` (L6214) на следующей итерации всегда истинно; единственный выход из цикла - `return` (L6221 или L6226). Сброс `firstPalletePosition` реально выполняется dispatch-веткой (L3343).

#### Подоперация unload_Pallete() (L5329-L5660), используемая Compact_R

1. `if (fifoLifo) fifoLifo_Inverse()` (L5332-L5333) - переключение inverse в начале; симметричный возврат на большинстве выходов (L5354, L5372, L5460, L5472, L5511, L5569, L5585, L5599, L5638, L5658).
2. `lifter_Down()` (L5339), `startDiff = 20` при `distance[0] < 90 + chnlOffset` (L5341-L5342).
3. Подход: `moove_Before_Pallete_R()` при `distance[2] > 750` (L5345-L5348) - подъезд к паллету со стороны начала канала, движением назад.
4. Движение назад к доскам (L5358-L5362, скорость 28/oldSpeed); передняя доска: `moove_Distance_R(dst, oldSpeed, 10)`, `dst = 600`, 670 для 1200, 500 для 800 при `channelLength - currentPosition - shuttleLength < 1500` (L5382-L5388).
5. Дожим maxbb: `2 + (150 - maxSpeed)/10`, +3 при distance[0]<300, -3 при 1200 (L5393-L5403), задержки 100 мс (L5406-L5416).
6. Ошибки: WARN_PALLET_SIZE_ERROR (L5432-L5441, `moove_Forward()` + CMD_STOP); WARN_OBSTACLE_AHEAD по таймауту `2000000/maxSpeed` или distance[0]<80 (L5453-L5462, с возвратом fifoLifo).
7. "Некуда везти": `distance[3] < 900` -> lifter_Down, WARN_PALLET_NOT_FOUND, `moove_Forward()`, CMD_STOP (L5477-L5485).
8. `lifter_Up()` (L5487), перехват dist 100/250/450 (L5495-L5499).
9. Для status != CMD_UNLOAD/LONG_UNLOAD/LONG_UNLOAD_QTY (наш случай) - `stop_Before_Pallete_F()` (L5633): перевозка к началу канала, дистанция постановки `dist/4 - interPalleteDistance - 100 - diff - mprOffset` (L4396), -25 для 1000/1200 (L4397), x0.96 (L4399).
10. `lifter_Down()` при `!longWork && lifterUp` (L5642), `lastPalletePosition = pstn + 800|1000 + interPalleteDistance` (L5644-L5648), `unloadCounter++` (L5656), возврат fifoLifo (L5657-L5658).

**Факт (утечка inverse):** два пути выхода `unload_Pallete` НЕ возвращают `fifoLifo`-инверсию: abort в цикле дожима (L5410-L5415, `preserveManualStopOnAbort` + return) и WARN_PALLET_SIZE_ERROR (L5432-L5441, return). При включённом `fifoLifo` глобальный `inverse` остаётся =1 после ошибки - фрейм координат последующих операций искажён (mirror `currentPosition = channelLength - currentPosition - 800` в `fifoLifo_Inverse` L4042 не выполняется повторно).

#### Stop/fault/abort

- `shouldAbortLoop()`: после `moove_Forward` (L6190-L6191), в подходе (L6200-L6205: motor_Stop + CMD_STOP), в цикле перестановки (L6222-L6227). `SystemYield()` на каждой итерации (L6198, L6216).
- `unload_Pallete` на abort-путях использует `preserveManualStopOnAbort()` (L8585-L8590: сохраняет CMD_STOP_MANUAL, иначе CMD_STOP) - в частности L5410-L5415.
- Faults/warnings - как у Compact_F плюс WARN_PALLET_NOT_FOUND (L5481).

#### Observable outcomes

- `compactCounter` (общий с Compact_F, L6219), `unloadCounter++` (L5656).
- `firstPalletePosition`: пишется в цикле (L6229), сбрасывается dispatch (L3343).
- `inverse`: при `fifoLifo` - циклическое переключение на каждый перенос; риск утечки (см. выше).
- Финал: CMD_STOP/STATE_IDLE как в 0.4/0.5.

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| Шаг подхода 100 мм, 25/25 | L6199 | configured | подъезд к штабелю |
| `distance[2] < 700` мм | L6195 | configured | условие подхода (паллетный reverse-ToF) |
| `distance[0] > 100 + chnlOffset` | L6196 | configured | guard заднего торца |
| `distance[0] < 150 && !lifterUp` | L6220-L6221 | configured | завершение операции |
| `dst = 600/670/500` | L5382-L5387 | configured | заезд под паллет (500 только 800 в коротком остатке) |
| `dist/4 - interPalleteDistance - 100 - diff - mprOffset`, x0.96 | L4396-L4399 | configured | постановка к началу канала |
| `distance[3] < 900` | L5478 | configured | "некуда везти" -> WARN_PALLET_NOT_FOUND |
| `2000000 / maxSpeed` мс | L5453 | configured | таймаут поиска |

#### Resource effects

Аналогично Compact_F; дополнительно: переключение `inverse` не меняет набор шин, но меняет mapping GPIO паллетных сенсоров в `detect_Pallete` (L3408-L3436) и знак скорости в `motor_Speed`/`motor_Stop` (L2113 и др.).

#### Профильные варианты 800/1000/1200

- `dst`: 600/670/500 (L5382-L5387, 500 - только 800 при коротком канале).
- maxbb `-3` при 1200 (L5395-L5402). Перехват 100/250/450 (L5495-L5499). `-25` к dist для 1000/1200 (L4397).

#### Unknowns

- Смысл пары `status = CMD_COMPACT_R` на L6213 (до цикла) и L6228 (в цикле) - избыточность или артефакт; из source назначение дублирования не устанавливается.
- Поведение при пустом канале: подход останавливается по `distance[2] >= 700`/отсутствию сенсоров, далее `unload_Pallete` действует по своим warning-путям; явного "nothing to compact" исхода нет.

#### Disposition proposals

- Мёртвый L6230-L6231: **exclude** (поведение уже покрыто сбросом в dispatch L3343).
- Мёртвый `moove_Forward()` в dispatch (L3345-L3346): **change** - см. Compact_F.
- Утечка inverse на error-путях unload_Pallete (L5410-L5415, L5432-L5441): **change** - гарантировать идемпотентный возврат кадра координат в V3 (или не переключать inverse на подоперацию).
- `compactCounter` до проверки результата: **change**.
- Ядро (перенос к началу канала через unload-подоперацию): **preserve**.

---

### 3. CountPallets (CMD_COUNT_PALLETS = 0x27)

#### Entry points и call sites

- Прототип: `L270`. Определение `pallete_Counting_F()`: `L6003-L6135`. Dispatch: `L3313-L3324`, call site `L3315`.
- Прямо вызываемые: `lifter_Down()` (L6006), `moove_Forward()` (L6009), `detect_Pallete()` (L6010/L6018/L6051/L6086), `get_Distance()` (L6017/L6031/L6050/L6085), `moove_Before_Pallete_R()` (L6021), `motor_Start_Reverse()` (L6032; `L2250-L2258`), `motor_Speed()` (L6033, L6099/L6124), `motor_Stop()` (L6026, L6045, L6094/L6117), `set_Position()` (L6062/L6071/L6132), `blink_Work()` (L6049, L6078), `SystemYield()` (L6041, L6077).
- В dispatch дополнительно: `moove_Forward()` (L3321) - возврат к началу канала после подсчёта.

#### Admission/preconditions

- Общий путь 0.3; supported на L8376. Аргументов нет. `palleteCount` (глобальный, `L603`) обнуляется в начале функции (L6007).

#### Шаги и переходы (normal path)

1. Лог "Start counting pallete forward..." (L6005), `lifter_Down()` (L6006), `palleteCount = 0` (L6007).
2. `moove_Forward()` (L6009) - проезд в начало канала; `detect_Pallete()` (L6010).
3. `palleteOnStart`: если ОБА forward- или ОБА reverse-сенсора сработали в начальной точке - паллет стоит под/у шаттла, `palleteOnStart = 1` (L6011-L6013). Abort-проверка (L6014-L6015).
4. Если впереди по каналу пусто (`distance[2] > 1000`) и ни один паллетный сенсор не сработал - `moove_Before_Pallete_R()` (L6019-L6021): подъезд к первому паллету вглубь канала. Если после этого `distance[0] < 150 + chnlOffset` - доехали до торца, паллет нет: `motor_Stop()`, return (L6024-L6027) - единственный "пустой канал" исход.
5. Старт прохода: `get_Distance()` (L6031), `motor_Start_Reverse()` (L6032), `motor_Speed(28)` (L6033) - движение назад сквозь канал.
6. Основной цикл `while (moove)` (L6039): `SystemYield()` (L6041); abort -> `motor_Stop`, `status = CMD_STOP`, return (L6043-L6047); `blink_Work()` (L6049), `get_Distance()` (L6050), `detect_Pallete()` (L6051).
7. Детект доски `detectPalleteR1 && detectPalleteR2` (L6052): `boardCount++` (L6054), debug-лог с временем между досками и позицией (L6055-L6059). Если `boardCount mod 3 == 1` (L6060): первая доска тройки - `set_Position()` (L6062), `boardPosition = currentPosition` (L6063), `palletePosition[palleteCount] = currentPosition` (L6065), `palleteCount++` (L6066), `lifetimePalletsDetected++` (L6067). Если `boardCount mod 3 == 0` (L6069): лог "Pallete width" (L6071-L6072).
8. Защита от двойного счёта: внутренний цикл `while (moove && (detectPalleteR1 || detectPalleteR2))` (L6075) - "проезд мимо" целого паллета: инкремент невозможен, пока хотя бы один reverse-сенсор сработал; выход из внутреннего цикла только когда оба сенсора освободились. Внутри: SystemYield (L6077), abort -> `preserveManualStopOnAbort()` + motor_Stop + return (L6079-L6083), измерение (L6085-L6086), регулировка скорости каждые 50 мс (L6087-L6101).
9. Регулировка скорости (одинакова во внутреннем и внешнем контурах, L6089-L6098 и L6108-L6125): `distance[0] <= 560+chnlOffset && > 120+chnlOffset` -> `distance[0]/20`; `<= 120 && > 80` -> 6; `<= 100 + chnlOffset` -> `motor_Stop`, `moove = 0`, лог "End channel on counting with pallete ..." (L6094-L6096 / L6117-L6119); иначе 28.
10. Финал (L6131-L6134): `palleteCount = lrint((float)boardCount / 3) + palleteOnStart` - **перезаписывает** инкрементальный `palleteCount` из L6066 (тот использовался только как индекс `palletePosition`); `set_Position()`; `channelLength = currentPosition + shuttleLength`; return.

#### Dispatch-эпилог (L3313-L3324)

После возврата: `status = CMD_STOP; send_Cmd();` (L3316-L3317) -> `status = CMD_COUNT_PALLETS;` (L3318) исключительно ради лог-контекста `makeLog(LOG_INFO, "Pallete count = %d", palleteCount)` (L3319) -> `status = CMD_MOVE_RIGHT_MAN;` (L3320) -> `moove_Forward()` (L3321) - возврат в начало канала -> `status = CMD_STOP; send_Cmd();` (L3322-L3323). Факт: промежуточные присваивания status не влияют на поведение `moove_Forward` кроме лога и ветки `status == CMD_LOAD/CMD_LONG_LOAD` внутри `moove_Forward` (L5247-L5249), которая здесь не срабатывает.

#### Stop/fault/abort

- Внешний контур: abort -> принудительный `status = CMD_STOP` (L6046) - CMD_STOP_MANUAL затирается. Внутренний контур: `preserveManualStopOnAbort()` (L6080) - CMD_STOP_MANUAL сохраняется. Факт асимметрии двух abort-путей одной функции.
- `SystemYield()` на каждой итерации обоих контуров (L6041, L6077).
- Отказ сенсора (ToF stale/fault) обрабатывается cross-cutting в `get_Distance`/safety -> `shouldAbortLoop`.
- Собственных setWarning/setFault у функции нет; warnings/faults возможны только из подфункций (`blink_Work` stall, lifter, ToF).

#### Observable outcomes

- `palleteCount` (L603) - финальное значение; транслируется в каждом пакете телеметрии (`pkt->palleteCount`, L8834) и логируется (L3319).
- `palletePosition[16]` (L595) - позиции паллет по первой доске каждой тройки; **запись без bounds-check** (L6065-L6066): при > 16 паллетах - выход за пределы массива. Факт.
- `lifetimePalletsDetected` (L6067, `ShuttleProtocol.h:L280`) - lifetime-счётчик, не сбрасывается.
- `channelLength` переопределяется (L6133).
- `boardCount` - локальная, не наблюдается; `countBoard` (L6036) используется только в debug-логе (L6055-L6059).
- После dispatch: шаттл в начале канала, `status = CMD_STOP`, `currentOperation = STATE_IDLE`.

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| Скорость прохода 28 | L6033, L6098/L6125 | configured | базовая скорость подсчёта |
| Зона `distance[0]/20` при 120<dist<=560 (+chnlOffset) | L6090-L6091 | configured | плавное торможение к торцу |
| Скорость 6 при 80<dist<=120 | L6092-L6093 | configured | доезд |
| Стоп при dist <= 100 + chnlOffset | L6094-L6096 | configured | конец канала |
| Квант регулировки 50 мс | L6087, L6106 | configured | период обновления скорости |
| `distance[2] > 1000` | L6019 | configured | признак пустого пространства впереди |
| `distance[0] < 150 + chnlOffset` | L6024 | configured | ранний выход "паллет нет" |
| `boardCount / 3`, lrint | L6131 | configured | 3 доски = 1 паллет (эвристика) |
| `boardCount mod 3 == 1` | L6060 | configured | запись позиции паллета |

#### Resource effects

- I2C ToF непрерывно (каждые ~50 мс `get_Distance`), GPIO-сенсоры с `delay(5)` debounce, CAN - скорости ID 100, телеметрия display, лог-буфер. Lifter - один раз вниз (L6006).

#### Профильные варианты 800/1000/1200

- В самой функции различий нет; профиль влияет косвенно через `shuttleLength` в `channelLength = currentPosition + shuttleLength` (L6133) и через `chnlOffset`/`oldSpeed` (configured).

#### Unknowns

- Физический смысл "3 доски на паллет" и надёжность эвристики для нестандартных паллет - из source не устанавливается.
- Назначение `palletePosition[]` далее не используется нигде в коде (единственная запись - L6065; чтений нет) - артефакт или задел под телеметрию, не устанавливается.
- Поведение при паллетах, стоящих вплотную (зазор < inter-pallet distance): двойной сенсорный контур должен их разделить, но граница не специфицирована.

#### Disposition proposals

- Эвристика 3 доски/паллет (L6131): **change/unknown** - в V3 подтвердить у заказчика допустимость; при сохранении - задокументировать.
- `palletePosition` без bounds-check (L6065): **change** - обязательная граница массива.
- Мёртвое использование `palletePosition` (нет читателей): **unknown** - выяснить, нужна ли позиция паллет в V3.
- Асимметрия abort (L6046 vs L6080): **change** - единая семантика preserveManualStop.
- Промежуточные status-присваивания в dispatch (L3316-L3320): **change** - в V3 лог не должен требовать фиктивной смены команды.
- Возврат в начало канала после подсчёта (L3321): **preserve** (контракт операции).

---

### 4. Demo (CMD_DEMO = 0x06)

#### Entry points и call sites

- Прототип: `L271`. Определение `demo_Mode()`: `L6693-L6833`. Dispatch: `L3308-L3312`, call site `L3310`.
- Прямо вызываемые: `lifter_Down()` (L6696), `moove_Reverse()` (L6697), `get_Distance()` (L6701/L6713 и во всех wait-циклах), `detect_Pallete()` (L6702), `moove_Distance_F(100, 25, 25)` (L6707), `motor_Speed()` (L6715/L6717), `load_Pallete()` (L6746), `unload_Pallete()` (L6795), `motor_Stop()` (множественно), `blink_Work()` (во всех wait-циклах), `SystemYield()` (множественно).
- Косвенно: весь набор из Compact_F и Compact_R (load/unload-подоперации).

#### Admission/preconditions

- Общий путь 0.3; supported на L8360. Аргументов нет.

#### Шаги и переходы

1. Лог "Start DEMO mode..." (L6695), `lifter_Down()` (L6696), `moove_Reverse()` (L6697) - к концу канала, `lastPalletePosition = 0` (L6698), abort-проверка (L6699-L6700).
2. Подход (L6704-L6719): `while (distance[3] < 700 && distance[1] > 100 + chnlOffset)` - шаги 100 мм вперёд (L6707), как у Compact_F, но БЕЗ условия паллетных сенсоров; флаг `moove = 1` (L6718).
3. **Внешний бесконечный цикл `while (1)` (L6720)** - фиксируется явно: функция не имеет естественного завершения и крутит load/unload-фазы до stop или fault.
4. Фаза загрузки `while (status != CMD_STOP)` (L6729): ожидание 1000 мс (пропускается на первой итерации при `moove == 1`, L6733), `load_Pallete()` (L6746), при `isErrorActive()` -> motor_Stop + return (L6747-L6751), при `distance[1] < 200` -> `status = CMD_STOP` (L6752-L6753) - паллеты в начале канала кончились.
5. Развилка (L6755-L6761): `(status == CMD_STOP && distance[1] > 200) || isErrorActive()` -> motor_Stop + return (внешний stop или ошибка); иначе `status = CMD_DEMO` - фаза считается завершённой штатно.
6. Пауза 2000 мс (L6763-L6776), abort-проверка (L6777-L6778).
7. Фаза выгрузки `while (status != CMD_STOP)` (L6779): ожидание 1000 мс (L6783-L6794), `unload_Pallete()` (L6795), `firstPalletePosition = currentPosition` (L6796), error-check (L6797-L6801), при `distance[0] < 200` -> `status = CMD_STOP` (L6802-L6803) - паллеты в конце канала кончились.
8. `firstPalletePosition = 0` (L6805). Развилка (L6806-L6814): продолжение при `!isErrorActive() && status == CMD_STOP && distance[1] < 200` -> `status = CMD_DEMO`; выход при `(status == CMD_STOP && distance[0] > 200) || isErrorActive()`; иначе `status = CMD_DEMO`.
9. Пауза 2000 мс (L6816-L6829), abort-проверка (L6830-L6831) - и обратно к L6720.

Условия выхода из `while(1)` (исчерпывающе): (а) `shouldAbortLoop()` в любой из ~10 проверок - CMD_STOP/CMD_STOP_MANUAL из UART (через SystemYield) или active fault; (б) `isErrorActive()` после load/unload/в развилках. Других выходов нет; reset/перезагрузка - только через внешний stop или fault.

#### Stop/fault/abort

- Stop: все abort-ветки делают `motor_Stop()` и return; часть дополнительно `status = CMD_STOP` (L6726, L6739, L6771, L6789, L6824) - избыточно при CMD_STOP, но затирает CMD_STOP_MANUAL (в отличие от preserveManualStopOnAbort, который demo_Mode не использует ни разу). Факт.
- Faults, которые могут latch-иться во время demo (все - через подфункции/cross-cutting, сама demo_Mode не вызывает setFault/setWarning):
  - `FAULT_MOTOR_STALL` (blink_Work L4078-L4085, `ShuttleProtocol.h:L183`);
  - `FAULT_LIFTER_TIMEOUT` (lifter_Down L2500-L2506 / lifter_Up);
  - ToF-faults `FAULT_TOF_CH_F/CH_R/PAL_F/PAL_R` (get_Distance/safety, tofFaultForSensor L3468-L3483);
  - `FAULT_AS5600`, `FAULT_LOW_BATTERY`, `FAULT_BUMPER_*` - cross-cutting через SystemYield.
- Любой fault -> `isErrorActive` -> demo выходит, `loop()` переводит в `CoreOpMode::ERROR` (L1819-L1820), принудительный останов.
- Warnings из подопераций (WARN_CHANNEL_FULL, WARN_PALLET_SIZE_ERROR, WARN_OBSTACLE_AHEAD, WARN_PALLET_NOT_FOUND) завершают текущую фазу через `status = CMD_STOP` внутри load/unload_Pallete; demo при этом может завершиться через развилки L6755/L6808 (если расстояние не соответствует штатному окончанию фазы).

#### Display/LED/buzzer

- `blink_Work()` (L4051): GREEN_LED + WHITE_LED мигание (L4088-L4091, L4106-L4111), CAN-drain (L4057), position-report каждые 10 тиков (L4058-L4062), телеметрия на тиках 11/19 (L4119-L4122).
- `blink_Warning()` (L4134): RED_LED + GREEN_LED + ZOOMER (buzzer) + BOARD_LED, 100 мс on/100 мс off - вызывается только из подопераций при warnings.
- Demo_Mode напрямую не управляет display-UART; телеметрия идёт через SystemYield/send_Cmd.

#### Observable outcomes

- Бесконечное челночное перемещение штабеля: все паллеты в конец канала (load-фаза), затем все в начало (unload-фаза), повтор.
- Счётчики: `loadCounter`/`unloadCounter` растут непрерывно; `compactCounter` НЕ растёт (load/unload_Pallete его не трогают).
- `firstPalletePosition` пишется в unload-фазе (L6796) и сбрасывается (L6805 + dispatch L3311).
- `lastPalletePosition` обнуляется на входе (L6698) и пишется подоперациями.
- Выход: `status = CMD_STOP` (или сохранённый stop), `currentOperation = STATE_IDLE` после AUTO_EXEC (L1906).
- Логи: "Start DEMO mode...", логи load/unload-подопераций, warning/error-логи подфункций.

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| Подход: `distance[3] < 700`, шаг 100 мм, 25/25 | L6704-L6707 | configured | подъезд к штабелю |
| Ожидание в фазе 1000 мс | L6733, L6783 | configured | пауза перед load/unload |
| Пауза между фазами 2000 мс | L6764, L6817 | configured | демонстрационная пауза |
| Конец load-фазы `distance[1] < 200` | L6752 | configured | паллеты кончились у начала |
| Конец unload-фазы `distance[0] < 200` | L6802 | configured | паллеты кончились у конца |
| Различение stop/natural: `> 200` | L6755, L6808 | configured | внешний stop vs штатный конец фазы |
| Двойной `SystemYield()` в паузах | L6766-L6767, L6819-L6820 | configured | факт: двойной вызов без комментария |

#### Resource effects

Как у Compact_F + Compact_R суммарно (непрерывно: CAN 100/101, I2C ToF, GPIO-сенсоры, LED/buzzer, телеметрия, лог-буфер). Особенность: неограниченная длительность - watchdog reload и все safety-проверки только через SystemYield; drain обоих UART только там же.

#### Профильные варианты 800/1000/1200

Собственных нет; наследуются от load/unload_Pallete (dst, перехваты, maxbb - см. секции 1-2).

#### Unknowns

- Поведение, если load-фаза не нашла ни одного паллета (пустой канал): `load_Pallete` идёт по warning-путям (WARN_OBSTACLE_AHEAD и т.п.) и ставит `status = CMD_STOP`; demo либо выходит через L6755 (distance[1] > 200), либо продолжает фазы - исход зависит от геометрии, из source однозначно не устанавливается.
- Назначение двойного `SystemYield()` (L6766-L6767, L6819-L6820) - не устанавливается.

#### Disposition proposals

- Бесконечный цикл с единственным выходом по stop/fault: **preserve** (демо-семантика), но в V3 явно специфицировать exit-контракт.
- Отсутствие send_Cmd в dispatch (L3308-L3312): **change** - унифицировать телеметрийные ACK-снапшоты.
- Затирание CMD_STOP_MANUAL в abort-ветках (L6726 и др.): **change** - использовать preserveManualStopOnAbort.
- `firstPalletePosition` в demo (L6796): **unknown** - используется только скоростным проездом moove_Before_Pallete_F; нужен ли в V3 demo - решить с заказчиком.

---

### 5. Взаимодействие с inverse/FIFO/LIFO (сводно по группе)

- `inverse` (L607), `fifoLifo` (L611) - persisted config (CFG_REVERSE_MODE=10, CFG_FIFO_LIFO=9: `ShuttleProtocol.h:L133-L134`; запись EEPROM L2990-L3003, L3065-L3066).
- `fifoLifo_Inverse()` (L4037-L4048): переключает `inverse`; при выключении зеркалит `currentPosition = channelLength - currentPosition - 800` (L4042) - константа 800 не зависит от профиля. Факт.
- **Compact_F**: `load_Pallete` не вызывает `fifoLifo_Inverse`; влияние inverse только через `detect_Pallete` (маппинг GPIO L3408-L3436), `set_Position` (знак дифференциала L3931/L3965/L3978/L4012), знак скорости в CAN (L2113 и др.), математику `moove_Distance_F/R` (L4859/L5060).
- **Compact_R**: `unload_Pallete` переключает inverse на входе (L5332-L5333) и возвращает на большинстве выходов; утечка на L5410-L5415 и L5432-L5441 (см. секцию 2).
- **Demo**: наследует поведение обеих подопераций; в unload-фазе inverse циклически переключается при `fifoLifo`.
- **CountPallets**: `fifoLifo`/`inverse` не использует напрямую; позиции считаются через `set_Position` (inverse-зависимая математика). `palleteCount` не зеркалится.
- Факт: `fifoLifo` не влияет на выбор направления компакта - режим FIFO/LIFO в V1 реализован только как инверсия кадра координат внутри unload-подопераций (и long_Load/long_Unload, вне scope).

### 6. Сводка мёртвого кода и ordering-багов группы (факты)

1. `Cntrl_V2/Cntrl_V2.ino:L3335-L3336` - мёртвый `moove_Forward()` (COMPACT_F), присваивание status раньше проверки.
2. `L3345-L3346` - мёртвый `moove_Forward()` (COMPACT_R), та же транспозиция; эталон живого порядка - L3354-L3356 (LONG_LOAD), L3303-L3305 (CALIBRATE).
3. `L6230-L6231` - недостижимый хвост `pallete_Compacting_R()` (`firstPalletePosition = 0` после цикла, из которого нет выхода кроме return).
4. `L6065-L6066` - запись `palletePosition[palleteCount]` без bounds-check (массив 16, L595).
5. `L5410-L5415`, `L5432-L5441` - выходы `unload_Pallete` без возврата fifoLifo-инверсии.
6. `L3316-L3320` - фиктивные промежуточные присваивания status ради лога.
7. `L3308-L3312` - единственная ветка dispatch без `send_Cmd()`.
8. `L6131` - инкрементальный `palleteCount` (L6066) перезаписывается финальной формулой; инкремент имел смысл только как индекс массива.
9. Закомментированные профильные поправки pltMaxLn (L5429-L5431, L5770-L5772) и `dst = 500` в load (L5728) - мёртвые комментарии.

---

## Группа: Home / Calibrate / Evacuate

Источник: локальное зеркало `C:/Projects/Shuttle/ShuttleController`, файлы `Cntrl_V2/Cntrl_V2.ino`, `Cntrl_V2/ShuttleProtocol.h` (plus `git grep` по всем production-файлам `Cntrl_V2/`).
Evidence SHA: `708d090980155d4a8d4644f7bcf87c383e81cd1d`. HEAD зеркала `4a226e5` на 1 коммит новее (только `docs/`); все цитируемые исходники идентичны evidence SHA. `docs/Controller-Nonblocking-Refactoring-Plan.md`, `tests/`, refactor-ветки не использовались.
Все anchors проверены чтением диапазонов. Формат: `файл:L<start>-L<end>`.

Общие факты (не повторяются в каждой разделе): кооперативный `SystemYield()` (Cntrl_V2.ino:L6839-L7058), `shouldAbortLoop()` (L8598-L8601), fault → core ERROR с принудительным остановом (L1819-L1820, L2050-L2058), ACK-коды (ShuttleProtocol.h:L136-L143), `send_Cmd()` шлёт telemetry только в `SerialDisplay` (Cntrl_V2.ino:L3400-L3403).

---

### Home (CMD_HOME = 0x07)

#### Entry points и call sites

- Объявление команды: `CMD_HOME = 0x07` в `enum CmdType`, блок "0x00 Block: Lifecycle & State" (ShuttleProtocol.h:L84, L94).
- Supported-список: `case CMD_HOME:` в `isSupportedCommand()` (Cntrl_V2.ino:L8351-L8384, кейс на L8361).
- Приём кадра: `processPacket()` (L2713), ветка `MSG_CMD_SIMPLE || MSG_CMD_WITH_ARG` (L2777-L2780); после всех admission-проверок `sendCommandAck(..., ACK_OK, ...)` и `return reqCmd` (L2855-L2873).
- Присвоение `status`: в `SystemYield()` при дренаже командных UART - `status = cmdRad` (L6987) / `status = cmdDisp` (L7018); `pollSerial` вызывается на L6950 (display) и L6954 (radio).
- Lifecycle: `loop()` (L1805), `case CoreOpMode::IDLE` (L1824): при `status != 0 && status != CMD_STOP` (L1826) - повторная проверка in-channel (L1829-L1841), затем `currentOperation = mapCmdToOperation(status)` (L1843), лог "Shuttle accepted CMD" (L1844), `send_Cmd()` (L1846), переход `currentMode = CoreOpMode::AUTO_EXEC` (L1870).
- Dispatch: `case CoreOpMode::AUTO_EXEC` → `run_Cmd()` (L1898-L1900); ветка Home в `run_Cmd()` (L3228): только `moove_Forward(); status = CMD_STOP; send_Cmd();` (L3391-L3395). Завершение AUTO_EXEC: `if (status != CMD_STOP) status = 0; currentOperation = STATE_IDLE; currentMode = CoreOpMode::IDLE;` (L1902-L1907).
- Физические функции прямо и косвенно:
  - `moove_Forward()` (L5197-L5265): `motor_Start_Forward()` (L5200; определение L2239-L2247, требует `ensureChannelTofReadyForMotion(true, "start_f")` L3536-L3553), `detect_Pallete()` (L5201, GPIO-дебаунс паллетных датчиков L3406+), при `lifterUp` - `stop_Before_Pallete_F()` (L5204, определение L4292+) и `lifter_Down()` (L5206), в цикле `SystemYield()` (L5215), `blink_Work()` (L5222), `get_Distance()` (L5223, ToF I2C), `set_Position()` (L5226, интегрирование угла AS5600, L3922-L4034), `motor_Speed(speed)` (L5258, CAN TX id=100, L2088+), `motor_Stop()` (L5264).
  - Отдельного homing-алгоритма и reference-сенсора нет: весь CMD_HOME - это `moove_Forward()`, общий с другими операциями.

#### Admission/preconditions

Путь в `processPacket()` (проверяются последовательно, первое срабатывание отвергает):

1. `isSupportedCommand` - CMD_HOME поддержан (L8361).
2. Provisioning: `!isProvisionedShuttle() && !isUnprovisionedCommandAllowed(reqCmd)` → `ACK_BAD_ENVIRONMENT` (L2819-L2828); CMD_HOME не входит в `isUnprovisionedCommandAllowed` (L8406-L8409).
3. Error: `isErrorActive() && !isOverrideCommand(reqCmd)` → `ACK_ERROR_STATE` (L2832-L2841); CMD_HOME не override (L8418-L8423).
4. In-channel: `inChannel = digitalRead(CHANNEL)` (L2841); `!inChannel && !isOutOfChannelExemptCommand(reqCmd)` → `setWarning(WARN_NOT_IN_CHANNEL, 5000)` + `ACK_BAD_ENVIRONMENT` (L2843-L2853); CMD_HOME не exempt (L8411-L8416).
5. Busy: `canAcceptCommandNow()` (L8430-L8462) - для CMD_HOME все специальные ветки не срабатывают, выполняется `return isShuttleIdle()` (L8462, L8425-L8428): приём только при `status == 0 || status == CMD_STOP`. Иначе `ACK_BUSY` (L2874-L2884).
6. Вторая проверка in-channel в `loop()` IDLE: тройное чтение CHANNEL с `delay(5)` (L1829-L1833); при провале - лог "Command 0x%02X rejected: Shuttle not in channel", `WARN_NOT_IN_CHANNEL`, `status = 0` (L1835-L1841).
Аргументов у команды нет (см. раздел о парсере).

#### Шаги и переходы (normal path)

1. Клиент получает `ACK_OK` (при radio + telem-флаг - `MSG_ACK_TELEM` с override `STATE_HOME` через `predictTelemetryStateForAcceptedCommand`, L2866-L2871, L8271-L8291; `mapCmdToOperation(CMD_HOME) = STATE_HOME`, L8258-L8259, STATE_HOME=17, ShuttleProtocol.h:L164).
2. `status = CMD_HOME` в SystemYield (L6987/L7018).
3. loop() IDLE: `currentOperation = STATE_HOME`, лог, `send_Cmd()` (telemetry на display), `currentMode = AUTO_EXEC` (L1843-L1870).
4. `run_Cmd()`: ветка HOME - сразу `moove_Forward()` без предварительного `send_Cmd()` (L3391-L3395).
5. `moove_Forward()` (L5197-L5265):
   - лог "Start moove forward... Status = %d" (L5199); `motor_Start_Forward()` (L5200); `detect_Pallete()` (L5201);
   - если `lifterUp`: `stop_Before_Pallete_F()` (движение к паллете и останов перед ней), затем `lifter_Down()`, `return` - до конца канала не доезжает (L5202-L5208);
   - цикл `while (moove)` (L5213): `SystemYield()` (L5215), abort-проверка (L5216-L5221), `get_Distance()` (L5223), каждые >50 ms (L5224): `set_Position()`, `detect_Pallete()`, clamp `currentPosition >= 0` (L5226-L5229), скоростной профиль по `distance[1]` (ToF до передней стены канала):
     - `distance[1] >= 1500` → `speed = 100` (L5230-L5233);
     - `90 + chnlOffset < distance[1] < 1500` → `speed = distance[1] / 20` c клампами 5..80 и гистерезисом по `oldSpeed` (L5234-L5242);
     - `distance[1] <= 90 + chnlOffset` → для HOME: `speed = 0; moove = 0; currentPosition = 60;` лог "End of channel, stop moove forward..." (L5246-L5257).
   - `motor_Speed(speed)` (L5258), вторая abort-проверка `if (shouldAbortLoop()) return;` без motor_Stop в этой точке (L5259-L5260), `motor_Stop()` по выходу (L5264).
6. Ветка HOME: `status = CMD_STOP; send_Cmd();` (L3393-L3394).
7. AUTO_EXEC-завершение: `currentOperation = STATE_IDLE; currentMode = IDLE` (L1906-L1907).

Физический смысл "home" в V1: движение вперёд до задней/передней стены канала, определяемой ToF-расстоянием `distance[1] <= 90 + chnlOffset` мм; позиция принудительно переустанавливается в константу 60. Отдельного homing-алгоритма, концевого выключателя или reference-сенсора нет; используется тот же механизм, что и для любой поездки вперёд.

#### Отличие CMD_HOME от CMD_MOVE_RIGHT_MAN (auto vs manual)

- Общая физика: оба вызывают одну и ту же `moove_Forward()` с одинаковыми условиями выхода (сравнение L3391-L3395 и L3237-L3243). В `moove_Forward` спец-кейс по status есть только для `CMD_LOAD/CMD_LONG_LOAD` (ползание speed=5 при distance>80, L5248-L5249); HOME и MOVE_RIGHT_MAN обрабатываются идентично.
- Отличия:
  1. Ветка MOVE_RIGHT_MAN в `run_Cmd` содержит дополнительный предварительный `send_Cmd()` (L3239); у HOME его нет (L3391-L3395).
  2. Telemetry-состояние: `CMD_HOME → STATE_HOME`, `CMD_MOVE_RIGHT_MAN → STATE_MANUAL` (L8258-L8265).
  3. Приём: MOVE_RIGHT_MAN от radio принимается только в `CoreOpMode::MANUAL`, от display - также из idle (L8437-L8445); HOME принимается из idle от любого источника (L8462).
  4. В MANUAL-режиме MOVE_RIGHT_MAN выполняется через `moove_Right()` (непрерывное ручное движение, L1913-L1916), а не через `run_Cmd`; HOME в MANUAL-режиме недостижим (отвергается как BUSY, т.к. `isShuttleIdle()` ложен при `status == CMD_MANUAL_MODE`).

#### Stop/fault/abort

- CMD_STOP/CMD_STOP_MANUAL, пришедшие во время движения, обрабатываются в `SystemYield()`: установка `status` + немедленный `motor_Stop()` (L6962-L6974). После этого цикл `moove_Forward` на следующей итерации проходит `shouldAbortLoop()` (L5216) → `preserveManualStopOnAbort()` (L8585-L8592: status=CMD_STOP, кроме CMD_STOP_MANUAL) → `motor_Stop()` → return (L5218-L5220).
- Любой active fault: `isErrorActive()` (L8593-L8596) → `shouldAbortLoop()` истинен → тот же abort-путь; плюс в `loop()` `currentMode = CoreOpMode::ERROR` (L1819-L1820), где `motor_Force_Stop()` если не остановлен (L2050-L2058).
- Отказ переднего канального ToF до старта: `motor_Start_Forward` не поднимает `motorStart` (`ensureChannelTofReadyForMotion` → `latchTofMeasurementFault`, L3536-L3553); движение не начинается.
- `SystemYield` вызывается в каждой итерации цикла движения (L5215); в ветке HOME вне `moove_Forward` его нет (всё выполняется в одном проходе `run_Cmd`).

#### Observable outcomes

- Успех: шаттл у стены канала, `currentPosition = 60` (L5254), `speed = 0`, `status = CMD_STOP`, `currentOperation` после завершения `STATE_IDLE` (L1906); лог "End of channel, stop moove forward..." (L5255); telemetry на display в моменты старта (L1846) и финиша (L3394) с `shuttleStatus = currentOperation` (L8828).
- Во время выполнения telemetry-состояние `STATE_HOME` (L1843, L8258-L8259).
- ACK: один `ACK_OK` при приёме (L2871); отдельных ACK начала/конца нет - только display-telemetry.
- Счётчики/логи: `palleteCount` обновляется `detect_Pallete()` пассивно, операция им не пользуется; предупреждений/фаултов нормальный путь не создаёт.

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| 50 ms | Cntrl_V2.ino:L5224 | configured | период управляющего цикла moove_Forward (ToF+позиция+скорость) |
| 90 + chnlOffset мм | Cntrl_V2.ino:L5246 | configured | порог останова у стены канала (chnlOffset - конфигурируемый CFG_CHNL_OFFSET) |
| 1500 мм | Cntrl_V2.ino:L5230 | configured | граница полной скорости (speed=100) |
| 60 | Cntrl_V2.ino:L5254 | configured | константа currentPosition при достижении "home" |
| 1000 ms | Cntrl_V2.ino:L1879-L1892 | configured | период фоновой telemetry в IDLE (не в AUTO_EXEC) |
| ~время доезда до стены | - | inferred | общего deadline у операции нет; длительность = путь/скорость, ограничена только fault/stop |

#### Resource effects

- CAN: команды мотору, TX id=100 в `motor_Speed` (L2088+, `CAN_TX_msg.id = (100)` L2096); дренаж CAN RX в `motor_Speed` (L2093-L2094) и в IDLE-ветке loop (L1877).
- I2C: ToF-сенсоры через `get_Distance()` (L3857+) каждую итерацию; AS5600 через `set_Position()`/сервис каждые 250 ms (cross-cutting).
- GPIO: паллетные датчики (`detect_Pallete`, L3406+), CHANNEL-пин (L1829-L1833), светодиоды (`blink_Work`).
- UART: display-telemetry (`send_Cmd`, L3400-L3403); приём команд display+radio в `SystemYield` (L6950, L6954).
- Flash: не используется.
- Blocking delays внутри цикла движения: отсутствуют (кроме `delay(5)` дебаунса в `detect_Pallete`).

#### Профильные варианты 800/1000/1200

Различий нет: ветвлений по `shuttleLength` в `moove_Forward`, ветке HOME и на пути приёма нет (проверено `grep`; ветвления 800/1000/1200 сосредоточены в load/unload-функциях, напр. L4397, L5383-L5399 - вне scope HOME).

#### Unknowns

- Физический смысл константы `currentPosition = 60` (почему не 0) из source не устанавливается.
- Какой конец канала считается "home" для клиента (V1 всегда едет только вперёд до стены).
- Поведение при `lifterUp` с точки зрения клиента (операция завершается перед паллетой, не у стены).

#### Disposition proposals (только предложения)

- preserve: приём только из idle, проверка in-channel, ACK_BUSY/ACK_ERROR_STATE/ACK_BAD_ENVIRONMENT - согласованы с уже задуманной V3-моделью admission.
- change: "home" как простая поездка вперёд до стены без отдельного homing-семантика - для V3 стоит явно определить, что означает HOME (поездка к стене vs позиция-референс), т.к. V1-поведение самостоятельной семантики не несёт.
- change: принудительная установка `currentPosition = 60` магией - заменить на обоснованный референс либо исключить.
- preserve: отсутствие отдельного reference-сенсора - V1 обходится ToF; новое железо из source не следует.
- change: отсутствие deadline у операции - в V3 оценить нужен ли общий watchdog операции.

---

### Calibrate (CMD_CALIBRATE = 0x16)

#### Entry points и call sites

- Объявление: `CMD_CALIBRATE = 0x16`, блок "0x10 Block: Core Movement" (ShuttleProtocol.h:L103).
- Supported-список: `case CMD_CALIBRATE:` (Cntrl_V2.ino:L8368) в `isSupportedCommand` (L8351-L8384).
- Dispatch: ветка `run_Cmd` (L3298-L3307):

  ```cpp
  send_Cmd();
  calibrate_Encoder_F();
  calibrate_Encoder_R();
  if (status != CMD_STOP)
      moove_Forward();
  status = CMD_STOP;
  send_Cmd();
  ```

- Telemetry-состояние: `STATE_CALIBRATE = 18` (ShuttleProtocol.h:L165), mapping L8260-L8261.
- Вызываемые физические функции: `calibrate_Encoder_F()` (L7342-L7455), `calibrate_Encoder_R()` (L7229-L7339), финальный `moove_Forward()` (L5197-L5265); внутри них: `moove_Distance_R(2000)` (вызов L7363; обёртка L4997-L5001; полная L5004-L5194), `moove_Forward()` (вызов L7245), `readAs5600AngleFresh()` (L1071-L1093), `motor_Start_Forward/Reverse` (L2239+), `motor_Speed(5)`, `SystemYield()`, `blink_Work()`.

#### Admission/preconditions

Идентичны HOME (подраздел "Admission" выше): supported (L8368), provisioning (L2819-L2828), не в error (L2832-L2841), in-channel (L2843-L2853), только из idle (`canAcceptCommandNow` → `isShuttleIdle()`, L8462). Аргумент игнорируется даже при `MSG_CMD_WITH_ARG` (L2857-L2864 - arg читаются только для manual-distance и LONG_UNLOAD_QTY).

#### Шаги и переходы (normal path)

1. `ACK_OK` (L2871), `status = CMD_CALIBRATE`, `currentOperation = STATE_CALIBRATE`, AUTO_EXEC (L1843-L1870).
2. `run_Cmd`: `send_Cmd()` (L3300) - telemetry STATE_CALIBRATE на display.
3. **calibrate_Encoder_F()** (L7342-L7455):
   1. лог "Start calibrating encoder to Forward" (L7344); дамп текущих массивов F и R в LOG_DEBUG (L7346-L7354); `delay(50)` (L7355).
   2. `if (inverse) inverse = 0;` (L7356-L7359) - принудительный сброс инверсии на время и после калибровки (не восстанавливается).
   3. Разгон назад: `moove_Distance_R(2000)` (L7363): обёртка использует maxSpeed=100, minSpeed=10 (L4999); фактическая цель 1950 мм (`dist > 500 → dist -= 50`, L5027-L5028); guard `distance[0] < 70` → лог "End of channel R, can't moove..." и пропуск движения (L5007-L5011); интеграция угла по массивам `calibrateEncoder_R` (L5099-L5135); стоп-условия: dist исчерпан, конец канала `distance[0] <= 90 + chnlOffset` (с `channelLength = currentPosition + shuttleLength`, L5147-L5152), no-progress watchdog 5000 ms → `FAULT_MOVE_TIMEOUT` + `motor_Stop` + `lifter_Down` + `status = CMD_STOP` (L5182-L5189); `shouldAbortLoop()` → `preserveManualStopOnAbort` + стоп (L5044-L5049). После: `if (shouldAbortLoop()) return;` (L7364-L7367).
   4. Fresh-чтение угла; при отказе `motor_Stop(); return;` (L7369-L7373) - отказ латчит `FAULT_AS5600` через `latchAs5600Fault("calibration_read", ...)` (L1087, L984-L1006, включая `motor_Force_Stop` при движении).
   5. Zero-seek вперёд: `motor_Start_Forward(); motor_Speed(5);` (L7374-L7375); цикл `while (!(angle > 4086 || angle < 10))` (L7379) - движение на скорости 5 до перехода угла через ноль; внутри: `SystemYield()` (L7381), abort → стоп+return (L7382-L7386), timeout 30000 ms → `setFault(FAULT_MOVE_TIMEOUT); motor_Stop(); return;` (L7387-L7391), повтор `motor_Speed(5)` каждые 100 ms (L7392-L7396), fresh-чтение угла не чаще чем каждые 5 ms, при отказе стоп+return (L7397-L7405).
   6. Sampling вперёд: `while (i < 8)` (L7413): timeout 120000 ms → `FAULT_MOVE_TIMEOUT` + стоп + return (L7415-L7419); `delay(3)` (L7421); поддержание скорости 5 каждые 100 ms (L7422-L7426); fresh-чтение угла на каждой итерации, отказ → стоп+return (L7427-L7431); захват сегмента при `angle` в окне `(i+1)*512 ± 10` (L7432): `calibrateEncoder_F[i] = millis() - cnt` (время между соседними переходами, L7434), накопление `summ`, `i++` (L7435-L7437); `SystemYield()`, `blink_Work()` (L7439-L7440). После 8 сегментов `motor_Stop()` (L7442).
   7. Постобработка (L7444-L7454): для каждого i: время заменяется нормализованной длиной сегмента `calibrateEncoder_F[i] = lrint(weelDia * 3.2 / 8 + t_i * weelDia * 3.2 / summ) / 2` (L7450, `weelDia = 100`, L1652), результат копируется в `eepromData.calibrateEncoder_F[i]` (L7452), дамп "Calibrate_F data:" в LOG_DEBUG (L7446, L7454).
4. **calibrate_Encoder_R()** (L7229-L7339):
   1. лог "Start calibrating encoder to Reverse" (L7231); fresh-чтение угла, отказ → `motor_Stop(); return;` (L7232-L7236).
   2. `if (inverse) inverse = 0;` (L7238-L7241).
   3. Позиционирование: `moove_Forward()` - доезд до конца канала вперёд (L7245); abort → return (L7246-L7252, содержит мёртвый `if (inverse) {}` L7248-L7250).
   4. Fresh-чтение угла (L7254-L7258).
   5. Zero-seek назад: `motor_Start_Reverse(); motor_Speed(5);` (L7259-L7260); тот же цикл `while (!(angle > 4086 || angle < 10))` (L7264) с timeout 30000 ms → `FAULT_MOVE_TIMEOUT` (L7272-L7277), re-speed 100 ms (L7278-L7282), угол каждые ≥5 ms (L7283-L7291).
   6. Sampling назад: `while (i < 8)` (L7298): timeout 120000 ms → `FAULT_MOVE_TIMEOUT` (L7300-L7305); `delay(3)` (L7306); re-speed 100 ms (L7307-L7311); fresh-чтение, отказ → стоп+return (L7312-L7316); окно `(7-i)*512 ± 10`, запись в `calibrateEncoder_R[7 - i] = millis() - cnt` (L7317-L7319); `SystemYield()`, `blink_Work()` (L7324-L7325); `motor_Stop()` (L7327).
   7. Постобработка: та же формула (L7334), `eepromData.calibrateEncoder_R[i]` (L7336), лог "Calibrate_R data:" (L7330, L7338).
5. Финал dispatch: `if (status != CMD_STOP) moove_Forward();` (L3303-L3304) - доезд вперёд до конца канала, если не было abort; `status = CMD_STOP; send_Cmd();` (L3305-L3306). Итог: шаттл у передней стены, `currentPosition = 60` (через moove_Forward L5254).

Условия циклов и выходы: все три движения - condition-driven без общего deadline операции; единственные временные пределы - 30 s zero-seek, 120 s sampling (×2 каждая фаза) и 5 s no-progress в moove_Distance_R.

Взаимодействие с maxSpeed/minSpeed: sampling-фазы используют логическую скорость 5, но `motor_Speed` преобразует её через глобальные `minSpeed`/`maxSpeed` (`hexSpeed.vint = minSpeed + oldSpeed * maxSpeed / 100 + ...`, L2088-L2113) - фактический выход мотора зависит от конфигурации; разгон `moove_Distance_R(2000)` параметрически 100/10 (L4999), но тот же `motor_Speed` дополнительно клапит рост скорости (L2099-L2102).

#### Сохранение результата

- Результат пишется в runtime-массивы `calibrateEncoder_F/R` (глобальные, L569-L574) и в `eepromData.calibrateEncoder_F/R` (поля L481-L482) - L7452, L7336.
- Автоматического сохранения во flash НЕТ: `pendingEepromSave` калибровкой не устанавливается (все установки: L2790 CMD_SAVE_EEPROM, L2935/L3069 config-set). Для персистентности клиент должен послать `CMD_SAVE_EEPROM` (0x30): парсер ставит `pendingEepromSave = true` и ACK_OK (L2788-L2793), запись выполняется в `loop()` только в покое `motorStart == 0 && motorReverse == 2` (L1811-L1815) через `saveConfigsToFlash()` (L7485-L7560; защита от записи в движении L7487-L7491 "FATAL: Blocked Flash Erase while moving!").
- КРИТИЧНЫЙ ФАКТ: при загрузке конфигурации из flash восстановление runtime-массивов ЗАКОММЕНТИРОВАНО (L7612-L7615):

  ```cpp
  /*for (uint8_t i = 0; i < 8; i++) {
    calibrateEncoder_F[i] = eepromData.calibrateEncoder_F[i];
    calibrateEncoder_R[i] = eepromData.calibrateEncoder_R[i];
  }*/
  ```

  Поэтому после reboot позиционирование всегда использует дефолт `{40,40,...}` (L569-L574), а не сохранённую калибровку; персистентные значения существуют только в `eepromData` (раунд-трип load/save, L7472, L7497-L7498) и не влияют на движение до повторного CMD_CALIBRATE. Дефолт `eepromData` при этом вычисляется как `lrint(weelDia * 3.4 / 8)` (L7576-L7580) - другое число, чем runtime-дефолт 40.

#### Stop/fault/abort

- CMD_STOP/STOP_MANUAL: каждый цикл калибровки содержит `SystemYield()` (дренаж команд, L6950/L6954) и `shouldAbortLoop()`:
  - moove_Distance_R: L5044-L5049 (`preserveManualStopOnAbort` + стоп);
  - zero-seek F/R: L7382-L7386, L7267-L7271 (`motor_Stop(); return;` без preserveManualStopOnAbort - status уже изменён самим SystemYield);
  - sampling F/R: неявно через `SystemYield()` (L7439, L7324) - явной проверки shouldAbortLoop внутри sampling-циклов нет; выход происходит по timeout/fault/отказу сенсора; abort после sampling обрабатывает следующий вызов (shouldAbortLoop-проверка в moove_Forward фазы R, L7246).
- Fault-пути: `FAULT_MOVE_TIMEOUT` (ShuttleProtocol.h:L183) на L7272/L7300 (R), L7387/L7415 (F), плюс L5184 в moove_Distance_R; отказ AS5600 → `FAULT_AS5600` (L1087, L984-L1006). Любой fault → `isErrorActive()` → loop переводит в `CoreOpMode::ERROR` (L1819-L1820, L2050+).
- Особенности потока: dispatch не проверяет error между `calibrate_Encoder_F()` и `calibrate_Encoder_R()` (L3298-L3307) - при fault в F всё равно вызывается R, которая сразу abort-ит своё движение через shouldAbortLoop; аналогично финальный `moove_Forward()` при `status != CMD_STOP` вызывается и сразу останавливается. К итоговому стопу это сходится, но промежуточные вызовы мотор-старта (`motor_Start_Forward` в L7374 и т.п.) при активном error успевают поднять `motorStart` до следующей abort-проверки.
- При отказе AS5600 в calibrate_Encoder_F (L7369-L7373) `status` остаётся CMD_CALIBRATE → dispatch продолжает R-фазу и финальный moove_Forward (см. выше).

#### Observable outcomes

- Успех: оба массива по 8 значений пересчитаны (единицы - мм сегмента, формула L7450/L7334), записаны в `eepromData` (без flash), шаттл у конца канала после финального `moove_Forward`, `currentPosition = 60`, `status = CMD_STOP`, `currentOperation = STATE_IDLE` после выхода из AUTO_EXEC (L1906).
- Логи: "Start calibrating encoder to Forward/Reverse" (LOG_INFO, L7344/L7231), дамп текущих массивов (LOG_DEBUG, L7346-L7354), "Calibrate_F data:"/"Calibrate_R data:" (LOG_DEBUG, L7454/L7338), лог moove-функций.
- Telemetry: STATE_CALIBRATE на время выполнения (L1843/L8260-L8261), ACK_OK один при приёме.
- Fault-исход: `FAULT_MOVE_TIMEOUT` или `FAULT_AS5600` → errorCode в telemetry (L8826), режим ERROR (L2050+), движение остановлено; несохранённые частичные массивы остаются в runtime до следующего старта (перезагрузка вернёт дефолт 40 из-за закомментированного восстановления).
- Побочный эффект: `inverse` сбрасывается в 0 (L7240, L7358) и в runtime не восстанавливается - до перезагрузки шаттл работает в неинвертированной ориентации датчиков даже при `eepromData.inverse == 1`.

#### Timing conditions

| Значение | Anchor | Класс | Смысл |
|---|---|---|---|
| 30000 ms | Cntrl_V2.ino:L7272, L7387 | configured | timeout zero-seek фазы (R и F) до FAULT_MOVE_TIMEOUT |
| 120000 ms | Cntrl_V2.ino:L7300, L7415 | configured | timeout sampling фазы (R и F) до FAULT_MOVE_TIMEOUT |
| 5000 ms | Cntrl_V2.ino:L5182-L5189 | configured | no-progress watchdog moove_Distance_R (разгон 2000 мм) |
| 3 ms | Cntrl_V2.ino:L7306, L7421 | configured | blocking delay на итерацию sampling |
| 100 ms | Cntrl_V2.ino:L7278, L7307, L7392, L7422 | configured | период повторной подачи motor_Speed(5) |
| ≥5 ms | Cntrl_V2.ino:L7283-L7291, L7397-L7405 | configured | минимальный период fresh-чтения AS5600 в zero-seek |
| 50 ms | Cntrl_V2.ino:L7355 | configured | delay перед стартом F-фазы |
| 2000→1950 мм | Cntrl_V2.ino:L7363, L5027-L5028 | configured | дистанция разгона назад перед F-фазой |
| ±10 (единиц угла 0-4095) | Cntrl_V2.ino:L7317, L7432 | configured | окно захвата сегмента (кратные 512) |
| speed 5 | Cntrl_V2.ino:L7260, L7375 | configured | логическая скорость zero-seek/sampling |
| 3000/5000 ms, distance[0]<70 мм | Cntrl_V2.ino:L5007-L5011, L5177-L5181 | configured | guard'ы moove_Distance_R |
| ~2×(30 s+120 s)+переезды | - | inferred | верхняя оценка длительности при худших таймаутах; типовая длительность из source не устанавливается |

#### Resource effects

- I2C: интенсивный опрос AS5600 (каждая итерация zero-seek/sampling, L7285/L7312/L7398/L7427), ToF через get_Distance в moove-функциях.
- CAN: motor_Speed(5)/ramp в moove-функциях (id=100).
- GPIO: detect_Pallete внутри moove_Forward/moove_Distance_R, blink_Work, CHANNEL.
- UART: display-telemetry (send_Cmd на L3300 и L3306), логи.
- Flash: только при последующем CMD_SAVE_EEPROM (L1811-L1815 → L7485+); сектор 7, 0x08060000, страницы 512 B (cross-cutting).
- Blocking delays: `delay(3)` на каждой sampling-итерации (L7306, L7421), `delay(50)` (L7355), `delay(5)` в detect_Pallete.

#### Профильные варианты 800/1000/1200

Различий нет: в calibrate_Encoder_F/R, moove_Distance_R и dispatch CMD_CALIBRATE ветвлений по `shuttleLength` нет (`shuttleLength` в moove_Distance_R используется только в формуле channelLength у стены канала, L5152, одинаково для всех профилей).

#### Unknowns

- Физический смысл нормализующей формулы `lrint(weelDia*3.2/8 + t_i*weelDia*3.2/summ)/2` (коэффициенты 3.2 и деление /2) из source не объясняется.
- Причина, по которой восстановление калибровки из flash закомментировано (L7612-L7615) - баг или намеренное решение - не устанавливается.
- Почему `inverse` не восстанавливается после калибровки.
- Требования к среде (пустой канал, груз) для валидности калибровки из source не следуют.

#### Disposition proposals (только предложения)

- preserve: двухфазная структура F+R с 8×512-сегментной моделью - согласуется с существующей моделью позиционирования (L3922-L4034).
- preserve: таймауты 30 s/120 s как fault-границы фаз.
- change: отсутствие автосохранения (CMD_SAVE_EEPROM обязателен) и закомментированное восстановление - в V3 персистентность калибровки должна работать end-to-end, иначе операция бесполезна между перезагрузками.
- change: dispatch без проверок между подфазами (L3298-L3307) - в V3 проверять fault/stop между фазами.
- change: сброс `inverse` без восстановления - в V3 явно определить политику ориентации при калибровке.
- exclude: `delay(3)` busy-loop в sampling - заменить на кооперативное ожидание при сохранении окна захвата.
- unknown: нужна ли валидация качества калибровки (разброс сегментов) - в V1 её нет.

---

### Evacuate (CMD_EVACUATE_ON = 0x28) - ОСОБЫЙ СЛУЧАЙ

**Вывод: V1 содержит только enum/evidence без production behavior - ПОДТВЕРЖДЕНО строго по source.**

Полный перечень всех мест встречи "evacuate" (case-insensitive) во всех production-файлах (`git grep -in evacuate -- Cntrl_V2/`, файлы: AlertManager.h, As5600HealthMonitor.h, As5600Sensor.cpp/.h, BmsDdA5.hpp, BmsDdA5FirmwareAdapter.hpp, Cntrl_V2.ino, E22Radio.hpp, ShuttleProtocol.h, TOF_Sense.cpp/.h, TofBusMonitor.h, TofHealthMonitor.cpp/.h, hal_conf_extra.h):

1. `CMD_EVACUATE_ON = 0x28,` - ShuttleProtocol.h:L114 (объявление в `enum CmdType`, комментарий блока "0x20 Block: Auto Operations").
2. `STATE_EVACUATE = 5,` - ShuttleProtocol.h:L152 (объявление в `enum ShuttleState`).

Ничего больше нет. В частности:

- (b) Supported-список: `isSupportedCommand()` (Cntrl_V2.ino:L8351-L8384) НЕ содержит `CMD_EVACUATE_ON` - switch перечисляет CMD_STOP..CMD_FIRMWARE_UPDATE, `default: return false;` (L8381-L8382).
- (c) Call sites: 0 в `.ino` и во всех остальных production-файлах; `STATE_EVACUATE` в `.ino` не встречается ни разу (grep по Cntrl_V2.ino - только ShuttleProtocol.h:L114 и L152).
- (d) Точный путь отвержения: кадр `MSG_CMD_SIMPLE`/`MSG_CMD_WITH_ARG` с cmdType=0x28 → `processPacket()` (L2713) → ветка L2777-L2780 извлекает `reqCmd` → `if (!isSupportedCommand(reqCmd))` (L2782) → `sendCommandAck(header->seq, ACK_REJECTED, replyPort, requiresNoAck, useTelemAck); return NO_NEW_CMD;` (L2784-L2785). Определение `sendCommandAck` L2697-L2711: при `suppressAck` (флаг NO_ACK в msgID) ответ не отправляется; при radio + telem-флаг шлётся `MSG_ACK_TELEM` (L2662-L2695), иначе `MSG_ACK` (L2632-L2660). Клиент получает `AckResult = ACK_REJECTED = 1` ("Generic rejection (reserved)", ShuttleProtocol.h:L139). `status` и `currentOperation` не изменяются, команда дальше парсера не проходит.
- (e) Состояние/enum/telemetry: `STATE_EVACUATE` не используется нигде вне объявления; `mapCmdToOperation()` (L8229-L8269) не имеет кейса для 0x28 (default → STATE_IDLE), но функция недостижима для этой команды из-за отвержения в L2782. Telemetry-пакет не содержит evacuate-полей (`populateTelemetryPacket`, L8821-L8850).

Мёртвый код: оба объявления (ShuttleProtocol.h:L114, L152) - зарезервированные идентификаторы без реализации; ветка отвержения L2782-L2786 - единственный production-путь, связанный с командой.

#### Disposition proposals (только предложения)

- unknown/owner-decision: семантика EVACUATE в V3 не может быть выведена из V1 (нет ни поведения, ни описания в source); если операция нужна - её спецификация создаётся с нуля, если нет - enum-значения можно исключить из протокола V3 либо сохранить как reserved.
- preserve (только как факт протокола): значения 0x28 и STATE=5 зафиксированы в wire-совместимом enum; любое переиспользование этих кодов в V3 сломает совместимость с клиентами, ожидающими ACK_REJECTED.

---

### Парсер: MSG_CMD_SIMPLE vs MSG_CMD_WITH_ARG для CMD_HOME/CMD_CALIBRATE

- Идентификаторы: `MSG_CMD_SIMPLE = 0x30` (1-byte payload), `MSG_CMD_WITH_ARG = 0x31` (5-byte payload) - ShuttleProtocol.h:L61-L62.
- Структуры: `SimpleCmdPacket { uint8_t cmdType; }` (ShuttleProtocol.h:L362-L365), `ParamCmdPacket { int32_t arg; uint8_t cmdType; }` (L368-L372).
- Различение: только по `realMsgID` кадра - `(realMsgID == MSG_CMD_SIMPLE) ? ((SimpleCmdPacket *)payload)->cmdType : ((ParamCmdPacket *)payload)->cmdType` (Cntrl_V2.ino:L2779-L2780). Никакой привязки типа сообщения к команде нет.
- Для CMD_HOME и CMD_CALIBRATE:
  - Оба типа сообщения принимаются одинаково; аргумент НЕ требуется и НЕ используется - `arg` читается только при `realMsgID == MSG_CMD_WITH_ARG` и только для `isManualDistanceCommand(reqCmd)` (mooveDistance) и `CMD_LONG_UNLOAD_QTY` (UPQuant) (L2857-L2864). CMD_HOME/CMD_CALIBRATE, посланные WITH_ARG, принимаются с молчаливым игнорированием arg.
  - Неверный формат: проверка длины payload под msgID отсутствует. `ProtocolParser` валидирует только sync, max-длину (`payloadLen > sizeof(rxBuffer) - sizeof(FrameHeader) - 2` → сброс, ShuttleProtocol.h:L553-L557) и CRC (L579-L596); при `payloadLen == 0` кадр считается валидным и уходит в `STATE_READ_CRC` (L558-L561). Следовательно кадр MSG_CMD_SIMPLE с length≠1 (в т.ч. 0) и MSG_CMD_WITH_ARG с length<5 всё равно достигают `processPacket`, где `cmdType`/`arg` читаются из фактического содержимого rxBuffer - поведение определяется мусором/остатками предыдущих кадров. Это наблюдение по source; конкретных последствий без динамического анализа не устанавливается (unknown).
  - Отвержение неизвестного/неподдерживаемого cmdType: ACK_REJECTED (L2782-L2786) - см. раздел Evacuate.

---

### Дополнительные зафиксированные аномалии/мёртвый код (по scope)

1. Закомментированное восстановление calibration-массивов из flash (Cntrl_V2.ino:L7612-L7615) - persisted калибровка не применяется после reboot (детали в разделе Calibrate).
2. Мёртвая ветка `if (inverse) {}` в abort-пути calibrate_Encoder_R (L7248-L7250).
3. Ветка `status == CMD_SAVE_EEPROM` в `run_Cmd` (L3325-L3329) и в ERROR-режиме (L2070-L2074) недостижима через парсер: CMD_SAVE_EEPROM обрабатывается целиком в `processPacket` (L2788-L2793, `return NO_NEW_CMD`), в `status` никогда не попадает. Фактический путь записи - `pendingEepromSave` (L2790, L1811-L1815).
4. Вторая abort-проверка в moove_Forward (L5259-L5260) выходит без `motor_Stop()`; безопасна только потому, что останов уже выполнен в `SystemYield` при установке CMD_STOP (L6973) либо выполняется ERROR-режимом при fault (L2054-L2057).
5. Sampling-циклы калибровки (L7298-L7326, L7413-L7441) не содержат явной `shouldAbortLoop()`-проверки - stop/abort во время sampling обрабатывается косвенно (SystemYield поднимает status/fault; выход происходит на следующей фазе/итерации по timeout или отказу сенсора).
6. `calibrateSensor_F/R` (ToF-калибровка) в `read_EEPROM_Data` из flash восстанавливается (L7616-L7620), а encoder-калибровка - нет; асимметрия зафиксирована, причина unknown.

---

## Сводные unknowns и validation obligations

Правило unknowns (issue 6): unknown блокирует нормативную спецификацию затронутой части, если способен изменить external semantics, authority/admission, safety invariant/safe reaction/recovery contract, hardware I/O contract или поведение поддерживаемого профиля. Остальные unknowns допускаются только как явные assumptions/validation obligations с владельцем, стадией и условием пересмотра.

### Блокирующие unknowns (require owner/hardware closure до нормативной спецификации)

| ID | Unknown | Класс влияния | Владелец | Стадия закрытия | Условие пересмотра |
|---|---|---|---|---|---|
| U01 | Физические единицы контракта приводов: скорость движения CAN ID 100 (условные 0-100, в кадре x1000), скорость лифтера CAN ID 101 (±50000), ток лифтера CAN ID 2405 (raw-единицы, порог перегрузки 500), множитель тормозного пути 15 мм/единицу скорости | hardware I/O contract | владелец + документация приводов | gate контрактов MoveDistance/LiftTo | новая ревизия приводов или результаты HIL-замеров |
| U02 | Размерность и допустимый диапазон аргумента MoveDistance (мм-подобная семантика не документирована; truncation int32 -> uint16 без валидации, поведение при arg <= 0 и arg > 65535 не специфицировано) | external semantics | владелец | gate контракта MoveDistance | изменение транспортного кодирования аргументов |
| U03 | Семантика Home: какой конец канала является «домом» для клиента; смысл принудительного currentPosition = 60; нужна ли V3 отдельная homing-семантика (в V1 это простая поездка вперёд до стены без reference-сенсора) | external semantics | владелец | gate контракта Home | появление reference-сенсора на PCB |
| U04 | Персистентность калибровки: почему восстановление encoder-массивов из flash закомментировано (`Cntrl_V2/Cntrl_V2.ino:L7612-L7615`) - баг или намеренное решение; смысл нормализующей формулы (коэффициент 3.2, деление на 2); требования к среде калибровки (пустой канал, груз) | external semantics + safety (точность позиционирования) | владелец | gate контракта Calibrate | замена датчика позиционирования в V3 |
| U05 | Семантика Evacuate: V1 содержит только enum `CMD_EVACUATE_ON = 0x28`/`STATE_EVACUATE = 5` и отвержение `ACK_REJECTED`; production algorithm отсутствует. Содержание новой V3-операции (источники запуска Safety Authority и Control Client по issue 9) выводится не из V1 | external semantics + safety | владелец | gate контракта Evacuate | изменение safety scope карты |
| U06 | Эвристика CountPallets «3 доски = 1 паллет» и её надёжность для нестандартных паллет; назначение `palletePosition[16]` (запись без bounds-check, читателей нет) | external semantics | владелец | gate контракта CountPallets | изменение механики паллет/каналов |
| U07 | Фактические runtime-значения конфигурации на эксплуатируемых устройствах: распределение профилей 800/1000/1200, maxSpeed, chnlOffset/mprOffset, interPalleteDistance, waitTime, калибровки (наследует системный индекс) | поведение поддерживаемых профилей | владелец + полевые данные | до декомпозиции профильных требований | появление production-дампов конфигурации |
| U08 | LongLoad не использует fifoLifo-инверсию (вызовов нет); является ли LIFO-загрузка intended через reverse-конфигурацию шаттла или это пробел V1 | external semantics LongLoad | владелец | gate контракта LongLoad | решение по унификации fifoLifo/inverse |
| U09 | Семантика ожиданий доставки unload: `distance[3] >= 800` без таймаута, пороги 900/600, пауза waitTime; ситуация «нет места впереди» сообщается как `WARN_PALLET_NOT_FOUND` (семантическое несоответствие) | external semantics UnloadPallet/LongUnload | владелец | gate контрактов unload-группы | изменение интеграции с конвейером/погрузчиком |
| U10 | Назначение расширенного стоп-паттерна motor_Stop при выезде с грузом (10 повторных zero-speed кадров x100 ms без SystemYield); менять только с проверкой на железе | hardware behavior | HIL-испытание | gate hardware-in-the-loop | замена привода/прошивки привода |

### Неблокирующие unknowns и assumptions

- Tuning-константы паллетных операций (600/670/500, 400, 750, 0.96, maxbb-формулы): сохраняются как verified production tuning; пересмотр - только вместе с HIL-валидацией и решением владельца по профилям.
- Lifted near-target escape (3000 ms при dist < 30 и lifterUp без fault): assumption, что это допустимое «дожатие у цели»; disposition change предложена - решение в gate MoveDistance.
- Поведение Demo при пустом канале и двойной `SystemYield()` в паузах: не устанавливается из source; Demo - демонстрационный режим, на нормативные контракты не влияет.
- Переполнение `int millis()`/`int count` после ~24.8 суток непрерывного аптайма: сквозной паттерн V1; assumption - контроллер перезапускается в сервисных циклах раньше; validation obligation для NFR availability.
- Повторная проверка `canAcceptCommandNow` в `SystemYield` после уже отправленного `ACK_OK`: команда может быть отброшена после подтверждения; факт зафиксирован, влияние - предмет контракта admission V3 (issue 13 уже фиксирует атомарный admission как цель).

## Дельты относительно системного индекса (impact set)

По правилу синхронизации issue 6 эти уточнения требуют обновления канонического системного индекса и impact review зависимых slices:

1. `WARN_CHANNEL_FULL` в одиночной загрузке фактически недостижим: `lastPalletePosition` обнуляется при приёме команды (`Cntrl_V2/Cntrl_V2.ino:L1845`), проверка в `load_Pallete` (`L5671-L5677`) срабатывает только в compact/long/demo-вызовах.
2. `CMD_EVACUATE_ON`: подтверждено отсутствие в supported-списке; точный путь отвержения `ACK_REJECTED` (`L2782-L2786`); других production-следов нет (ровно 2 вхождения enum).
3. Мёртвые ветки dispatch COMPACT_F/R (`L3335-L3336`, `L3345-L3346`): «выезд вперёд после уплотнения» в V1 не выполняется.
4. Утечка `inverse = 1` при fifoLifo на двух abort/warning-путях `unload_Pallete` (`L5410-L5415`, `L5432-L5441`) - последующие движения физически инвертированы.
5. Восстановление encoder-калибровки из flash закомментировано (`L7612-L7615`): после reboot действует дефолт {40x8}; ToF-калибровка при этом восстанавливается (`L7616-L7620`).
6. Восстановление `inverse` после CMD_CALIBRATE отсутствует (`L7240`, `L7358`).

Обновление `docs/research/v1-system-evidence-index.md` выполнено отдельным шагом (issue 44) с impact review per-operation tickets #17-#29; в `main` файл промоутнут тикетом issue 55.

## Независимый checklist-review по gate legacy evidence ready

Независимая техническая ревизия отдельной сессией, не участвовавшей в извлечении evidence (2026-08-04). Проверено по первоисточнику `Driadix/ShuttleController@708d090980155d4a8d4644f7bcf87c383e81cd1d`.

**Итог gate: PASS.**

Результаты по пунктам checklist (issue 6):

1. Entry points и call sites - PASS: dispatch `run_Cmd()` прочитан целиком (`Cntrl_V2/Cntrl_V2.ino:L3228-L3397`), MANUAL-ветки loop (`L1911-L2048`) сверены, косвенные call sites `load_Pallete`/`unload_Pallete` подтверждены grep (single_Load, compact F/R, long load/unload, demo).
2. Normal / rejection / stop / fault / recovery paths - PASS: для каждой операции доведены до наблюдаемого результата; recovery описан ссылкой на канонический системный item (допустимо в модели bundle).
3. Source/authority, provisioning, lifecycle, профили - PASS: display/radio-асимметрии, provisioning-правила, IDLE/MANUAL/AUTO_EXEC/ERROR и все профильные ветки 800/1000/1200 сверены с grep по `shuttleLength ==`.
4. Сверка с системным индексом - PASS: shared state, CAN/I2C/UART, actuator ownership, persistence и safety paths без молчаливых противоречий; дельты заявлены явно в impact set выше.
5. Каноничность общих фактов - PASS: cross-cutting факты применяются со ссылкой на канонические items; повторяемые константы консистентны во всех разделах.
6. Dispositions - PASS: каждый раздел содержит предложения preserve/change/exclude/unknown с оговоркой «предложения, не решения»; факты отделены от суждений.
7. Configured vs inferred - PASS: классы выдержаны; нигде не заявлены measured или доказанные end-to-end bounds.
8. Unknowns - PASS: блокирующие U01-U10 классифицированы по правилу риска с владельцем, стадией и условием пересмотра; неблокирующие оформлены как assumptions/validation obligations.

Выборочная проверка anchors: 38 групп anchors прочитаны по первоисточнику (требовалось >= 15), все утверждения о поведении кода подтверждены. Покрытие операций 13/13; `Evacuate` подтверждён как enum-only (ровно 2 вхождения в production-файлах).

Findings ревизии и их обработка: 5 minor-неточностей в номерах строк anchors без семантических ошибок (lifter_Down ramp abort: 2469 -> фактическая 2477; `diff < 0` clamp: 4936 -> 4933-4934; `diff != 0` блок: 4936-4937 -> 4935-4940; диапазон `ParamCmdPacket`: 366-371 -> 367-371; граница `run_Cmd` в сводном скелете: 3396 -> 3397) - исправлены точечной правкой в настоящей версии. Блокирующих ошибок не найдено.
