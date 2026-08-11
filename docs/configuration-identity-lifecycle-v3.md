# Configuration, Identity & Lifecycle V3 (Production Lifecycle, item 7)

Статус: **утверждено владельцем (Q1–Q8, 2026-08-11)**; резолюция — тикет «Спроектировать production lifecycle V3 (item 7)» (#50 карты #1). Вход в logical item 7 `Configuration, Identity & Lifecycle` нормативного пакета (issue 8): shuttle profiles, provisioning, calibration, persistent data, time/identity, update/rollback/recovery. Покрытие gates: G2 (safety-relevant части: INV-UPDATE-STATIONARY, HZ-13/14/15, restart-семантика — опираются на #45/#46) и G3 (contract-level: конфигурация, update, identity, время).

Термины — канонические из `CONTEXT.md`; исполнение — cooperative scheduler (issue 10), ports-and-adapters (#43), safety-модель (#45), lifecycle-оси (#46), protocol (#47), бюджеты (#48), наблюдаемость (#49), semantic contract (#13).

## 1. Scope и входы

Решено: flash-карта, update-архитектура (bootloader, слоты, активация, commit/rollback), key-модель, identity, модель конфигурации, калибровка (персистентность), RTC/время, Service/recovery-режимы, provisioning-потоки. Вне scope: электроника/PCB (frozen), реализация прошивки, дисплей/пульт/мост/WMS, сертификация, bootloader-side serial recovery (физический сервис — последний рубеж).

Входы: #45 (HZ-13/14/15, recovery-классы, INV-BRAKE-VALIDITY), #46 (оси, restart-таблица, admission-матрица), #47 (Service/Update-классы, authority, SetWallClock), #48 (flash-бюджеты, watchdog, app ≤ 512 KB → реконсиляция), #49 (журнал диагностики, Backup SRAM, timeValidity), #13 (admission/epoch), #51 (toolchain).

## 2. Flash-карта (F405, 1 МБ, сектора 0–3×16K + 4×64K + 5–11×128K)

| Область | Сектора | Адреса | Размер | Владелец / контракт |
| --- | --- | --- | --- | --- |
| Bootloader (verified boot) | s0–4 | 0x08000000–0x0801FFFF | 128 KB | иммутабелен в поле; читает/пишет только status sector |
| Слот A (app_A) | s5–6 | 0x08020000–0x0805FFFF | 256 KB | link base 0x08020000 |
| Config journal | s7 | 0x08060000–0x0807FFFF | 128 KB | config + calibration; V1 keep; GC эрэйзит только s7 (суточная stats-копия — в s11, чтобы GC s7 практически не наступал) |
| Слот B (app_B) | s8–9 | 0x08080000–0x080BFFFF | 256 KB | link base 0x08080000 |
| Status sector | s10 | 0x080C0000–0x080DFFFF | 128 KB | slot pointer, pending, bootcount, accepted_counter, rollback_flag; 2 копии + counter rotation (otadata-паттерн); общий контракт bootloader ↔ Persistence adapter |
| Diagnostics journal | s11 | 0x080E0000–0x080FFFFF | 128 KB | лимиты 5/25 записей в сутки (fallback #49) + суточная flash-копия stats (#49; вынесена из s7 — F2) |

Следствия:
- GC config-журнала (стирание s7, ≤ 4 s, W_flash) физически не задевает status (s10) — раздельные сектора обязательны: на F405 erase посекторный. Записи s7 — единицы в год (provisioning, калибровка, ConfigSet persist) → **GC s7 практически не наступает**; power-cut в окне GC s7 (erase+rewrite ~4–6 s) = полная потеря конфигурации/калибровки → **принятый residual в рамках HZ-14** (Unprovisioned + Degraded + re-commissioning); окно задокументировано, тест power-cut mid-GC обязателен (F2).
- App-бюджет образа: **≤ 252 KB** (256 − заголовок+TLV ~1 KB − резерв ~3 KB; direct-boot не использует swap-trailer — F5). Реконсиляция бюджета 512 KB из #48: вынуждена раскладкой 1024 = 128 + 256 + 128 + 256 + 128 + 128. Условие пересмотра: образ > 252 KB на proving slice #54 (фолбэк — пересмотр секторации с отклонением «V1 keep»).
- Wear status sector: ~128 пар по 512 Б на цикл стирания × ~10k циклов ≈ 1.2M обновлений (обновления редки — только вокруг update).
- Watchdog: reload между последовательными flash-операциями; одиночное окно erase+program ≤ 4.013 s < 6.8 s fast-конца LSI (#48).
- SRAM-развилка (RAM-exec flash-драйвер vs принятие окна) — измерение на #54; 192 KB SRAM достаточно для RAM-драйвера + staging-буферов.

## 3. Update-архитектура: 2-слот direct-boot

### 3.1 Модель

- **Два раздельно слинкованных и раздельно подписанных артефакта** на релиз: `app_A` (link base 0x08020000) и `app_B` (link base 0x08080000). PIC/fixups не используются — два линк-скрипта, два бинарника. Смена указателя/VTOR недостаточна для исполнения образа из чужого слота (absolute addressing Cortex-M).
- **Активация** = запись slot pointer + pending (слова flash в status sector) → reset. `W_apply` (определение для #54): от подтверждённой quiescence до первого шага scheduler нового образа = запись указателя (µs) + reset (ms) + verified boot (P256, ~100–300 мс [INFERENCE]) + init; **бюджет ≤ 1 s**, измерение — обязательство #54.
- **Verified boot при каждом старте**: bootloader проверяет SHA256 + ECDSA-P256 подпись выбранного слота и `header.load_address == base слота`; невалидный слот → **персистированный фолбэк**: одна otadata-запись `{pointer → другой слот, pending=0, rollback_flag}` → reset. Указатель всегда отражает исполняемый слот — иначе UpdateStatus/recovery-таргет расходились бы с исполнением, и стейджинг писал бы в исполняемый слот (на single-bank F405 это erase-while-executing, F1). Оба слота невалидны / порча status → физический сервис (ROM-bootloader/ST-Link).
- Bootloader иммутабелен в поле (обновление — только физический доступ); сетевого стека в bootloader нет.

### 3.2 Формат образа (MCUboot-модель)

Заголовок `{magic, format_version, semver (major.minor.patch+build), размер, load_address, флаги}` + TLV-область `{SHA256, ECDSA-P256 signature, security counter}`. `Finalize` валидирует: размер ≤ 252 KB, checksum, подпись, `image.counter ≥ accepted_counter` (равенство допускается — пере-релиз с тем же counter), **load_address == base неактивного слота по персистентному указателю** (mismatch ⇒ reject). **`accepted_counter` на Finalize/staging НЕ повышается — только при commit** (иначе не загрузившийся образ с большим counter заблокировал бы auto/manual rollback: старый слот с меньшим counter был бы отвергнут verified boot). `UpdateStatus` раскрывает неактивный слот (по персистентному указателю) — клиент (bridge) присылает артефакт под него. Обязательные contract-тесты: артефакт слота A отклоняется при целевом слоте B и наоборот; Finalize с `image.counter < accepted_counter` → reject; Finalize не изменяет `accepted_counter`; фолбэк персистируется (указатель == исполняемый слот).

### 3.3 Update-ось и окна (#46, механика)

- `Idle → Staging → Applying → Committed | RolledBack | Failed`, `Staging → Aborted`; весь lifecycle — окно `Update` (INV-UPDATE-STATIONARY, motion закрыт, read-only диагностика разрешена).
- **Staging**: образ по Update-классу (network_bridge, Update Authority; reserved capacity), bulk-запись в неактивный слот — длинные flash-окна здесь (quiescence C6, reload watchdog между операциями, bumper → latch + post-window). Abort / отказ валидации / latched fault → `Aborted` (marker не ставится, staging-область отбрасывается по CRC); power-cut → reboot в `Idle`.
- **Applying**: вход — quiescence C6, ¬Fault; маркер активации — flash-резидентный (status sector); активация = pointer + pending → reset; power-cut в Applying → boot с pending → счётчик попыток.
- **Commit/rollback**: целостность образа + bounded boot attempts (**N=3**). Commit-точка (решение владельца, простота > квалификационные окна): **после успешного setup + первого завершённого тика основного цикла (планировщик)** — health-независимо (#46: health-ось и crash-маркер в критерий не входят; латчированный fault при дошедшем до цикла boot не блокирует commit — откат не помог бы: старый образ фолтит так же). Reset до commit (любая причина, включая power-cut — неразличимы на F405) → attempt++ (status sector); исчерпание → auto-rollback на следующем boot: флип указателя, pending/counter сброс, **rollback_flag ставится в той же записи** → `RolledBack` → окно Serving (старый слот нетронут, его квалификация не требуется — был committed ранее). Приложение на стартапе читает rollback_flag и эмитит событие с rollback-причиной (#49 durable events, F8); флаг сбрасывается после эмиссии. Soak-интервалы и runtime checkpoints отклонены владельц…
- **Ограничение (принято владельцем)**: поздние смерти после commit не откатываются. Покрытие: ручной откат к релизам с тем же `accepted_counter` (policy поднятия counter — только security-релевантные релизы, §4; обычные релизы сохраняют counter → откат к предыдущему релизу работоспособен) + пере-релиз с текущим counter (F6) + события/журнал (rollback-причина, #49).
- **Ручной откат**: Update-команда (Update Authority, network_bridge); guard = Update-класс по §9.2 (Serving + Ready/Degraded + Provisioned + слот + stationary); из Fault — последовательность ClearFault (qualified baseline) → ManualRollback; к образу с counter < accepted → `DowngradeDenied` (§4) (F7).
- **Failed** (порча нового слота после Finalize; порча status → физический сервис, §3.4/§9.3 — F10): bootloader грузит старый слот → окно `Recovery` (Degraded, HZ-15) → ограниченная поверхность (раздел 9.3).

### 3.4 Status sector (s10)

Фиксированный формат, общий контракт bootloader ↔ Persistence adapter: `{slot pointer, pending, bootcount, accepted_counter, rollback_flag}` в 2 копиях + счётчик записи (otadata-паттерн: расхождение копий разрешается счётчиком; MAGIC записывается последним). Bootloader пишет: bootcount++, персистированный фолбэк (pointer + pending=0 + rollback_flag), сброс pending; приложение пишет: pointer+pending (активация), **commit = одна атомарная запись `{pointer, pending=0, accepted_counter=max(accepted_counter, image.counter)}`**. **GC status sector — владелец приложение** (pre-erase на quiescence в Serving; bootloader никогда не стирает: erase в Boot съедает бюджет Ready ≤ 5 s и нарушает правило #49 «erase-capable операции в Boot не выполняются» — F4). При sector-full (практически недостижимо: ~2000 обновлений на цикл стирания) bootloader продолжает boot без инкремента bootcount (деградация счётчика задокументирована) и событийно не уведомляет — приложение при первом quiescence выполняет GC. Порча status → оба слота невалидны-эквивалент → физический сервис.

## 4. Key-модель

- **Одна пара ECDSA-P256**: публичный ключ зашит в bootloader (иммутабелен); приватный ключ — в build-pipeline владельца; оба A/B-артефакта подписываются одним ключом. Root-signed manifest иерархии отклонён владельцем (экономия ресурсов, парк ≤ 32 шаттлов).
- **Security counter (анти-даунгрейд)**: монотонный `accepted_counter` в status sector — **пол** для verified boot: образ принимается при `image.counter ≥ accepted_counter` (равенство допускается). **Повышение — только при commit** (атомарно с очисткой pending); Finalize/staging счётчик не трогают. Следствия: (а) не загрузившийся образ с большим counter не блокирует auto/manual rollback (accepted ещё старый); (б) после commit откат к образу с меньшим counter **невозможен by design** (анти-даунгрейд) — manual rollback к нему отклоняется на admission (`DowngradeDenied`); при необходимости вернуть старую версию — пере-релиз с текущим counter. Общий на релиз (оба A/B-артефакта).
- **Policy поднятия counter (F6)**: counter поднимается **только на security-релевантных релизах** (MCUboot-практика); обычные релизы сохраняют counter → резидентный ручной откат к предыдущему релизу остаётся рабочим (§3.3). Код `DowngradeDenied` регистрируется в wire-реестре rejection-кодов #47 — obligation на обновление item 6 (G3).
- **Задокументированный риск**: компрометация ключа / ротация = физический reflash парка. Смягчение: compound-условие атаки (ключ + доступ к network_bridge с грантом Update Authority, #47), ручной откат, события/журнал (#49).
- **Obligation (production policy, hardware-verified при реализации)**: WRP option bytes для boot-региона (s0–s4) и production RDP policy — проверяются на реальном железе против RM0090 и фактического поведения части перед включением; семантика option-byte в этот дизайн не утверждается.

## 5. Identity

- **`deviceSerial`**: производный от UID96 (0x1FFF7A10, hex, read-only, без записи в NVM) — условный уникальный идентификатор устройства для диагностики/эксплуатации/производственной трассировки.
- **`shuttleNum` (1..32)**: операционная (складская) идентичность — адрес радио (0x01xx), привязка provisioning; переназначение — re-provisioning (раздел 10). 0 = unprovisioned.
- **Identity-блок** в query/observability: `{shuttleNum, deviceSerial, profile, firmware semver, controllerEpoch}`.
- MAC-эмуляция и сертификаты отклонены (channel-trust #47, crypto на wire вне core v3).

## 6. Модель конфигурации (Config & Profile)

### 6.1 Журнал (s7)

- **Record-журнал NVS-стиля**: запись = `{key, len, data, CRC16}`, метаданные после данных (атомарный фиксатор), erase-front wear-кольцо, integrity-скан при boot. **Персистентность — снапшот-записями (F3)**: одна логическая единица = одна запись (provisioning-блок, калибровочный блок, config-снапшот, stats-снапшот); атомарность — по семантике записи (данные → метаданные → CRC; power-cut mid-record → CRC невалиден → предыдущая запись остаётся валидной). Загрузка: последняя валидная запись каждой единицы + групповая валидация кросс-инвариантов; «откат к committed snapshot» (#46 §3) реализуется выбором предыдущей валидной записи — вне GC-окна (GC практически не наступает, §2). Один компонент Persistence adapter; **два инстанса**: config (s7, namespace `config:*`, `calib:*`) + diagnostics (s11, формат #49 + namespace `stats:*` — суточная flash-копия stats, перенос из s7 устраняет регулярный GC конфигурации, F2). V1-ротация 256×512 Б заменена (multi-page калибровка; один формат журнала).
- **Schema versioning**: `schema_version` (u16) в заголовке; на load — цепочка миграций; неизвестная (более новая) схема или невозможная миграция → конфиг невалиден → `Unprovisioned` + Degraded (HZ-14-семейство) + re-provisioning; покрывает откат прошивки с более новой схемой (safe mode, не кирпич).
- **Save policy — explicit only**: persist-intent на `ConfigSet/Sync` (#47), `PersistConfiguration` (quiescence C6), атомарный коммит provisioning (одна снапшот-запись), auto-persist калибровки (раздел 7), суточная flash-копия stats (#49, в s11). V1 deferred-auto-save отменён; несохранённые live-изменения теряются при power-cut (задокументировано, клиент повторяет).
- **Журнал полон (s7) — `JournalFull` (F11)**: persist-операции → reject со стабильным кодом `JournalFull` (регистрация в реестре #47 — obligation) + событие; deferred GC — при ближайшей quiescence (C6); live-изменения не блокируются. Аналог `journalFull` #49.
- Одно авторитетное состояние (#43): RAM-состояние Config & Profile, journal — durable-копия; двойного shadow нет.

### 6.2 Классы параметров и каталог v1

| Класс | Параметры | Источник/валидация |
| --- | --- | --- |
| Identity | `shuttleNum` 1..32 (0 = unprovisioned) | provisioning; диапазон; радио-адрес 0x01xx производный |
| Профиль | `profileId` 800/1000/1200 — firmware-resident bundle (не NVM-редактируемый) | выбор валидируется; U07 (фактическое распределение профилей в поле) — владелец + полевые данные, non-blocking |
| Геометрия | wheel radius и др. параметры геометрии | provisioning; диапазоны; кросс-инварианты профиль↔геометрия; смена → пере-выполнение calibration validity gate |
| Калибровка | encoder-таблицы F/R, sensor-калибровка, offsets, ToF-калибровка | раздел 7 |
| Stats | lifetime-счётчики (Backup SRAM + суточная flash-копия в s11, #49) | Persistence adapter |
| Tuning-константы | класс «configured» algorithmic docs (ReverseRunStartGuard, SegmentCaptureWindow, CalibrationSpeed и пр.) — **compile-time, не пользовательские** | не входят в каталог конфигурации |

Полная нумерация id/тип/диапазон с V1-anchors — при наполнении item 6/7 (schema tables, contract tests), каркас выше нормативен.

### 6.3 Валидация

Per-param диапазоны на admission (reject-коды #47); кросс-инварианты профиль↔геометрия; plausibility калибровки (validity gate, раздел 7). Все изменения конфигурации — mutating service (guards раздел 9).

## 7. Калибровка: персистентность end-to-end

- **Восстановление при boot**: Config & Profile грузит калибровочные таблицы из journal (s7) в стартапе после validity gate — фикс V1-дефекта (`Cntrl_V2.ino:7612-7615`, восстановление закомментировано). Исторический вопрос «почему закомментировано» — за гейтом контракта Calibrate (#40).
- **Auto-persist на `Succeeded` Calibrate**: journal-запись (снапшот-запись калибровочного блока) в терминальной фазе (stationary, quiescence C6, окно ms-класс ~1–2 KB) — end-to-end персистентность (change-предложение #40); при исчерпанном резерве журнала терминальная фаза может включать GC-окно ≤ 4 s (редко: GC s7 практически не наступает, §2/F2) — бюджет фазы это учитывает (F9); `PersistConfiguration` остаётся для прочих параметров; повторный persist калибровки идемпотентен.
- **Calibration validity gate** при load: CRC journal + plausibility (сегменты > 0, сумма ≈ длина канала в допуске, sensor-калибровка/offsets/геометрия в диапазонах) → невалиден ⇒ **Degraded** + ограничение positioning-зависимых capability (одометрия, MoveDistance и пр.; список — на гейте контракта Calibrate #40); гейт `Provisioned` не блокируется; тупика нет — Calibrate доступен при Degraded (#46). Пороги — policy-параметры Config & Profile: дефолты от V1-evidence (`{40…40}`), уточнение при commissioning (U07/U01, non-blocking).
- **Update/rollback калибровку не инвалидирует** (#46 restart-таблица); миграции — через schema_version (раздел 6).
- **U04-остатки** (смысл нормализующей формулы, требования среды калибровки: пустой канал, груз) — non-blocking obligation на гейт #40 с владельцем и стадией закрытия (правило #8).

## 8. RTC/время

- **Политика: аппаратно-независимый best-effort RTC, без новых требований к PCB.**
- **Fallback-время (модель владельца)**: wall clock = последнее синхронизированное время, продолженное RTC; синхронизация — периодически при подключении bridge (SetWallClock, #47; NTP через внешний backend); постоянного подключения к NTP не предполагается; отдельного механизма ре-синхронизации на контроллере нет.
- **timeValidity (#49)**: `Synced` после успешного SetWallClock в эпохе; `RTC-only` = последнее сохранённое время, продолженное RTC (эмитится; возможен дрейф); `Unsynced` при потере RTC (init state / backup reset flag при boot) — wall не эмитится; V1-дефолт 2023-01-01 валидным временем не считается.
- **SetWallClock**: только RTC, monotonic не трогается, plausibility 2020–2100, authority Service Client, per-epoch `syncedThisEpoch`. NTP-скачок не ломает monotonic-семантику (host-тест, #49 #9).
- **Доменные таймауты — только monotonic (#43)**, не зависят от wall-clock.
- **Obligation (hardware-verified при commissioning)**: наличие/работоспособность LSE (популяция, старт осциллятора), проводка VBAT и удержание RTC/backup-домена при снятии питания, фактический дрейф RTC. До измерения дрейфа компенсация в прошивке не вводится; точность — на отчётности `timeValidity=RTC-only`.

## 9. Service/recovery-режимы

### 9.1 Поверхность Service-класса

`ConfigGet/Set/Sync` (live/persist), `PersistConfiguration` (quiescence C6), `SetWallClock` (#47), `ClearFault` (отдельный guard, ниже), `FactoryReset`, `ManualRollback` (Update Authority), `DiagnosticsCapture` (fault-capture #49), Update-транзакция `Begin/StageChunk/Finalize/Abort/Status` (Update Authority, network_bridge). Query/read-only — Control-класс, окон не требует.

### 9.2 Guards и окна

| Класс | Start guard | Примечание |
| --- | --- | --- |
| Обычный mutating service | Serving + Ready/Degraded + слот свободен (#46) | в Fault заблокирован |
| Provisioning (initial) | Serving + любая health (вкл. Fault) при Unprovisioned | HZ-13/14 необратимы (#46) |
| **ClearFault** | **Serving + Fault** + qualified baseline (#45/#40: квалифицированное восстановление sensing/шины, физическая неподвижность) + слот + Service authority | после очистки health → Initializing → Ready/Degraded (requalification); в Ready/Degraded неприменима (reject); единственный mutating explicit-reset (power-cycle ≠ acknowledgment) |
| Update | Serving + Ready/Degraded + Provisioned + слот → handoff окно Update | Update Authority |
| Recovery-рефлэш | окно Recovery (Degraded, HZ-15): update-class + read-only | перепрошивка битого слота артефактом под его base (таргет — неактивный слот по персистентному указателю §3.1; load_address-чек против него, §3.2) |
| Read-only | Serving/Update/Recovery, health любое | #46 |

### 9.3 FactoryReset и Recovery

- **FactoryReset = generation-bump** в config-журнале (s7): одна атомарная запись `configGeneration++` → все записи `config:*`/`calib:*` старых поколений игнорируются (логический wipe; физический reclaim — лениво при GC); power-cut-safe. **Status sector (s10) и Backup SRAM-маркеры не трогаются** (маркер снимается только ClearFault — семантика power-cycle ≠ acknowledgment не нарушается). Guards: Serving + Ready/Degraded + stationary + слот + Service authority. Результат: `Unprovisioned` + дефолты, motion закрыт (HZ-13), **без Degraded** (намеренное действие, не HZ-14-corruption). Событие в журнал (#49).
- **Recovery**: `Failed` → bootloader грузит старый слот → окно Recovery: поверхность = update-class (перепрошивка битого слота) + read-only → `Committed` → Serving. **Оба слота невалидны / порча status → физический сервис** (ROM-bootloader/ST-Link) — последний рубеж.

### 9.4 Commissioning

Brake-тест (Q7.1 A) + ТО re-test — процедура сервисного персонала: firmware предоставляет авторизованные motion-примитивы + диагностику (force-stop frame observation, drive timeout); **регистрация результата — вне firmware** (журнал ТО, INV-BRAKE-VALIDITY). Детали — раздел 10.6.

## 10. Provisioning-потоки

### 10.1 First-boot

Чистый journal → `Unprovisioned` + дефолты (профиль не выбран); `deviceSerial` (UID96) доступен; motion закрыт (HZ-13); поверхность: provisioning (bridge) + read-only; радио — дефолтный адрес.

### 10.2 Initial provisioning (network_bridge)

Service authority (grant); flow: присвоение `shuttleNum` (1..32) + выбор профиля (валидация firmware-resident bundle) + геометрия → **атомарный journal-commit одной снапшот-записью provisioning-блока (C6)** → `Provisioned` (power-cut mid-record → CRC-невалиден → предыдущая запись остаётся, F3). Калибровка — после, операцией Calibrate (требует Provisioned, #46). Bridge-only: присвоение identity по недоверенному radio = spoofing-вектор.

### 10.3 Re-provisioning (bridge)

Смена номера/конфигурации: Serving + Ready/Degraded + слот свободен + **stationary = C6-предикат на момент commit** (слот удерживается — движение не может начаться между admission и commit; деталь #46 закрыта). Провал/power-cut → откат к последнему валидному снапшоту (предыдущая запись нетронута — вне GC-окна, §6.1/F2/F3) или `Unprovisioned` (initial). Событие в журнал (#49).

### 10.4 Смена shuttleNum по радио (требование заказчиков, V1-паритет)

- Service-grant на effective radio profile; те же guards (Serving + Ready/Degraded + слот + stationary/C6); атомарный commit.
- **Радио-адрес 0x01xx применяется live после коммита** — текущий radio-линк рвётся; операторский пульт переподключается на новый адрес (задокументировано; смена номера идёт по radio, не по bridge).
- Остальная provisioning-поверхность (профиль/геометрия/FactoryReset) — bridge-only.
- **Принятый residual risk**: radio = channel-trust (любой E22-модуль на той же частоте/адресе может послать смену номера). Смягчение: grant-требование, guards, атомарность, событие в журнал; crypto на wire — вне core v3 (#47).
- Расширение до полного radio-provisioning — задокументированная extension point, не проектируется.

### 10.5 Смена геометрии/профиля

Validity gate калибровки пере-выполняется (раздел 7); невалид → Degraded + рекалибровка.

### 10.6 Комиссионирование (процедура)

1. Bench flash: bootloader + app_A/app_B (производственный процесс, физический доступ; WRP/RDP policy — obligation раздела 4).
2. Initial provisioning (bridge): shuttleNum + профиль + геометрия.
3. Calibrate (radio или bridge; auto-persist).
4. Brake/force-stop commissioning-тест (Q7.1 A; внешняя регистрация, INV-BRAKE-VALIDITY).
5. Передача в эксплуатацию.

## 11. Взаимодействия (сводка)

- Restart-таблица #46 дополняется: status sector (pointer/pending/bootcount/counter) — переживает reboot и power-cut (flash); config/calibration — переживают (journal); wall clock — best-effort (раздел 8).
- Window-handoff: `Update → Serving` при оси ∈ {Committed, RolledBack, Aborted}; commit происходит в первые тики после активации — длинного окна Update после boot нет.
- Epoch: runtime-сущности умирают; persisted (config, status, markers) — не зависят (#46 I-LC-6).
- Fault при активном update в Staging → Aborted (окно Serving, health Fault); в Applying невозможен (¬Fault на входе, quiescence C6).
- Удаление research-веток и commit-pinned evidence — #55 (не зависит от этого документа).

## 12. Validation obligations (вне этого тикета, владельцы)

| Obligation | Владелец/стадия |
| --- | --- |
| Измерение `W_apply` (≤ 1 s бюджет), flash stall, ISR latency, RAM-exec развилка | proving slice #54 |
| Размер образа V3 ≤ 252 KB (условие пересмотра раскладки) | #54 / реализация |
| WRP s0–s4 + production RDP policy (hardware-verified) | production policy, реализация |
| VBAT/LSE проводка, старт осциллятора, удержание при power-off, дрейф RTC | commissioning, hardware |
| Power-cut во время update (staging/applying) — обязательный тест | #52/#54 (acceptance #50) |
| Contract-тесты: A/B artifact load_address mismatch; pointer/pending/commit; bootcount exhaustion → rollback; FactoryReset generation-bump; ClearFault admission; **Finalize не повышает accepted_counter; commit повышает атомарно; rollback к меньшему counter → DowngradeDenied; фолбэк bootloader персистируется (указатель == исполняемый слот); rollback_flag → событие rollback-причина; power-cut mid-provisioning → старый снапшот; power-cut mid-GC s7 → HZ-14-путь; JournalFull → reject + событие; status sector-full → boot без bootcount (деградация)** | verification pyramid #52 |
| U04-остатки (формула нормализации, среда калибровки) | гейт контракта Calibrate #40 |
| U07 (фактическое распределение профилей), U01 (единицы приводов) | владелец + полевые данные |

## 13. Отклонённые варианты

| Вариант | Отклонено | Основание |
| --- | --- | --- |
| A/B swap (MCUboot swap-using-offset) | активация = минуты, противоречит атомарному окну #46 | direct-boot (ms) |
| Single-slot + download (overwrite) | не влезает в 1 МБ; power-cut → необратимо (ESP: unsafe mode) | раскладка |
| ROM-bootloader как штатный | ROM-UART ненадёжен (факт владельца), BOOT-пины запрещены | #46 |
| Root-signed key manifest | владелец: экономия ресурсов, парк ≤ 32 | Q2a |
| Soak/checkpoint commit | владелец: переусложнение; простой commit после первого тика | Q2 |
| Health-gated commit | #46: health-ось вне критерия | Q2 |
| Bootloader-side serial recovery | сетевой стек в bootloader; дублирует app-поверхность | Q7 |
| V1-ротация 256×512 Б | multi-page калибровка; два формата журнала | Q4 |
| Auto-save конфигурации (V1 deferred) | неявные записи; #47 даёт явный persist-intent | Q4 |
| NVM-редактируемые профили | профиль — контракт железа | Q4 |
| Полный provisioning по radio | spoofing при channel-trust (кроме смены номера — требование) | Q8 |
| Авто-провижининг по UID96 | складская логика — за shuttleNum | Q8 |

## 14. Assumptions и Unknowns

- **Assumption:** erase/program flash на F405 останавливает prefetch и задерживает ISR (поведение кремния; конфигурация этой PCB не проверена) — #43.
- **Assumption:** P256-верификация ~100–300 мс [INFERENCE] — вписывается в Ready ≤ 5 s (#48); измерение #54.
- **Unknown:** фактический размер образа V3 (V1-оценка 150–300 KB; бинарь V1 не измерен) — условие пересмотра раскладки.
- **Unknown:** VBAT/LSE/backup-domain поведение на этой PCB — obligation раздела 8.
- **Unknown:** семантика WRP/RDP option bytes для этой части при RDP1 — obligation раздела 4.
- **Unknown:** U04-остатки, U07, U01 — владельцы в разделе 12.

## 15. Ссылки

- Тикет «Спроектировать production lifecycle V3 (item 7)» (#50) и resolution-комментарии Q1–Q8 (включая коррекции по независимым ревью: A/B-артефакты, ClearFault admission, namespace-scoped FactoryReset, status sector).
- #45 (safety/health, HZ-13/14/15, recovery-классы, INV-BRAKE-VALIDITY), #46 (оси, restart-таблица, admission), #47 (protocol: Service/Update surface, authority, SetWallClock), #48 (бюджеты: flash, watchdog, очереди), #49 (наблюдаемость: журнал, Backup SRAM, timeValidity), #13 (semantic contract), #43 (границы: Config & Profile, Update Authority, Persistence), #8 (item 7, gates G2/G3), #40 (Calibrate, U04), #51 (baseline).
- V1-свидетельства: `Driadix/ShuttleController@708d090` (config EEPROM-эмуляция s7, калибровка L7612-7615, MSG_SET_DATETIME, BKPSRAM-статистика).
- Индустриальные источники: RM0090 (flash-организация F40x, UID96), DS8626, AN4657/AN4894/AN2606, MCUboot design.md/bootutil, ESP-IDF OTA (otadata, rollback), U-Boot bootcount, Zephyr NVS/settings.

## 16. Ревью

Независимое экспертное ревью №1 (до фиксации, rule issue 8): найденные blockers учтены выше — A/B-артефакты direct-boot (absolute addressing); ClearFault admission (дедлок восстановления); FactoryReset vs status region (erase посекторный); status region в отдельном секторе (GC config-журнала); accepted_counter повышается только при commit (иначе rollback заблокирован).

Независимое экспертное ревью №2 (2026-08-11, отдельная сессия, reviewer-агент): вердикт **APPROVED WITH FINDINGS — 3 MAJOR / 8 MINOR / 1 NIT, 0 BLOCKING**; flash-карта, watchdog-математика, wear-оценки и anti-downgrade контракт подтверждены. Все 12 находок закрыты правками документа:

- **F1 (MAJOR)**: персистированный фолбэк bootloader'а (указатель == исполняемый слот; таргет UpdateStatus/recovery по нему) — §3.1/§3.2/§9.2.
- **F2 (MAJOR)**: stats-копия → s11; GC s7 — принятый HZ-14-residual (окно ~4–6 s, практически не наступает); тест power-cut mid-GC — §2/§6.1/§10.3/§12.
- **F3 (MAJOR)**: снапшот-записи (одна логическая единица = одна запись; откат к предыдущей валидной) — §6.1/§10.2/§10.3.
- **F4 (MINOR)**: GC status sector — владелец приложение (pre-erase на quiescence); bootloader не стирает; sector-full → boot без bootcount — §3.4.
- **F5 (MINOR)**: бюджет образа 252 KB (256 − header/TLV − резерв; без swap-trailer) — §2/§3.2/§12.
- **F6 (MINOR)**: policy поднятия counter (security-релевантные релизы); DowngradeDenied → реестр #47 — §3.3/§4.
- **F7 (MINOR)**: guard ManualRollback = Update-класс; путь из Fault — ClearFault → ManualRollback — §3.3/§9.2.
- **F8 (MINOR)**: rollback_flag в status; событие rollback-причина на стартапе — §3.3/§3.4.
- **F9 (MINOR)**: терминальная фаза Calibrate может включать GC-окно ≤ 4 s при исчерпанном резерве — §7.
- **F10 (MINOR)**: триггер Failed = порча слота после Finalize (порча status → физический сервис) — §3.3.
- **F11 (MINOR)**: политика `JournalFull` для config-журнала — §6.1.
- **F12 (NIT)**: коррекция anti-downgrade контракта и итоги ревью зафиксированы на тикете #50 (issue 8 §9) — запись опубликована.
