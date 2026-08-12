# Lifecycle-оси и переходы V3 (Configuration, Identity & Lifecycle / Safety & Health / External Semantic & Transport)

Статус: **Approved - profile qualification rollback/recovery rebaseline [«Переутвердить профильный scope релиза v1.0.0»](https://github.com/Driadix/ShuttleControllerV3/issues/59): независимый embedded/RT review APPROVE (0 findings) и owner approval на ревизии `9b431a7` (PR #78)**. Предыдущая revision утверждена владельцем (Q1-Q6) и независимо reviewed в тикете [«Спроектировать lifecycle-оси и переходы V3»](https://github.com/Driadix/ShuttleControllerV3/issues/46). Вход в logical items 5 (`Safety & Health`), 6 (`External Semantic & Transport Contracts`) и 7 (`Configuration, Identity & Lifecycle`) нормативного пакета (issue 8). Термины - канонические из `CONTEXT.md`; исполнение - cooperative scheduler с bounded steps (issue 10), ports-and-adapters (#43), safety-модель (#45), semantic contract (#13).

## 1. Модель: независимые оси

Lifecycle моделируется **шестью независимыми осями** (issue 2): платформенное окно, provisioning-статус, operation lifecycle, health, session, update. `Idle/Running` — проекция operation lifecycle (нет/есть активный root-экземпляр), `Error` — health-состояние, а не глобальный mode. Operating contexts (startup, commissioning, normal, manual, service/diagnostics, update, fault recovery) — композиции значений осей, не отдельный enum.

Инварианты модели:

- **I-LC-1 (single-writer):** каждая ось имеет одного владельца-писателя: окно — execution core; provisioning — Config & Profile (+ Persistence adapter как исполнитель journal); operation lifecycle — Operation Runtime; health — Safety Authority (latch) / Sensing (health-состояния сенсорики, классификация — Safety по snapshot'ам, #43); session — Manual Session; update — Update Authority.
- **I-LC-2 (детерминизм):** каждый переход оси — guard над snapshot'ами, проверяется на границе bounded шага; гонок между осями нет (кооператив, #10).
- **I-LC-3 (host-тестируемость):** все переходы и guards формулируются как чистые функции состояния → проверяются host-тестами без Arduino/RTOS.
- **I-LC-4 (один эксклюзивный слот):** в любой момент ≤ 1 активная Exclusive Control Activity из {motion-операция, manual-сессия, mutating service, update}; safety supervision активна всегда.
- **I-LC-5 (молчаливых терминаций нет):** отказ admission — стабильный rejection code (`ResourceConflict` и др., #13); активные сущности завершаются только явным stop/fault-путём или reboot.
- **I-LC-6 (epoch):** reboot выдаёт новый controllerEpoch; runtime-сущности (operations, ledger, session) умирают с epoch; persisted-сущности (provisioning, конфигурация, update-маркеры, Backup SRAM) от epoch не зависят (#13).

## 2. Платформенное окно (владелец: execution core)

```text
Boot → Serving → Update → Serving | Recovery → Serving | Boot
(любое окно) → Boot        (reset: watchdog / HardFault / power-cycle / ручной restart)
```

- Reset/power-cycle: **любое окно → `Boot`** — стартап всегда reset-cause-aware (#45): crash-маркер → health `Fault` после реконсиляции reset-cause, иначе `Initializing → Ready/Degraded`; новый epoch выдаётся при выходе из Boot.

- **Boot**: инициализация адаптеров, подъём control plane, выдача epoch; внешняя поверхность закрыта. Health — `Initializing`.
- **Serving**: control plane доступен (admission/query/subscription/read-only). **Окно НЕ означает допуск движения** — движение гейтится health-осью (Ready/Degraded + motion-инварианты), provisioning-осью и эксклюзивом. `Serving + Fault` допустим: read-only диагностика (breadcrumbs) доступна, motion закрыт (INV-FAULT-ADMISSION) — необходимо для осмотра marker-стартапа.
- **Update**: весь update lifecycle (staging включён) — INV-UPDATE-STATIONARY (#45, V1 keep); motion закрыт; read-only диагностика разрешена. Handoff: `Serving → Update` — запрос Update Authority через admission (тикет #46, Q4), исполняет execution core.
- **Recovery**: неработоспособный образ после failed update (HZ-15) либо ранее committed fallback image, не квалифицирующий persisted `configuredProfileId` (`ProfileQualificationMismatch`, #50 §3.3/§11); health - Degraded; active profile отсутствует, motion закрыт; ограниченная Update/Service-поверхность (recovery-перепрошивка) + read-only; выход - успешное восстановление совместимым signed image → `Serving`, иначе retry.
- **Fault — не состояние окна**: health-ось ортогональна окну.
- Service/диагностика — не состояние окна: read-only поверхность живёт в Serving/Update/Recovery, mutating-сервис — через эксклюзивный слот.

Переходы (guards):

| Переход | Guard |
| --- | --- |
| `Boot → Serving` | инициализация адаптеров завершена, epoch выдан, watchdog armed |
| `Serving → Update` | запрос Update Authority, admission: слот свободен, stationary (INV-UPDATE-STATIONARY), health ∈ {Ready, Degraded} |
| `Update → Serving` | update-ось в терминале `Committed`, `Aborted` либо `RolledBack`, если fallback image квалифицирует persisted `configuredProfileId` (#50 §3.3/§11) |
| `Update → Recovery` | update-ось в `Failed` либо в несовместимом `RolledBack` с `ProfileQualificationMismatch` |
| `Recovery → Serving` | recovery-перепрошивка успешна (`Committed`), health-гейты пройдены |
| `Recovery → Boot` | ручной restart из recovery |

## 3. Provisioning-ось (identity + motion-relevant persisted-конфигурация)

```text
Unprovisioned → Provisioning → Provisioned
Provisioned → Provisioning        (re-provisioning)
```

- **Unprovisioned**: нет валидного identity (номера) ИЛИ нет валидной persisted-конфигурации; поверхность — только Service (provisioning-операции) + read-only queries; motion закрыт (HZ-13).
- **Provisioning**: транзиентный bounded flow записи во flash: присвоение номера, смена номера, запись persisted-конфигурации (профиль 800/1000/1200, радиус колеса и другие параметры геометрии). Admission: только provisioning-поверхность; эксклюзивный слот занят.
- **Provisioned**: валидный identity + валидная persisted-конфигурация; полная поверхность.

Переходы (guards; атомарность — HZ-14 journal, владелец Persistence adapter):

| Переход | Guard / правило |
| --- | --- |
| `Unprovisioned → Provisioning` | авторизованная provisioning-операция (Service Client) |
| `Provisioning → Provisioned` | полная валидация (профиль + конфигурация + identity) успешна; новая journal-запись закоммичена |
| `Provisioning → Unprovisioned` | initial provisioning: abort/валидация не прошла/power-cut — rollback-таргета нет, defaults |
| `Provisioning → Provisioned` (rollback) | re-provisioning: abort/power-cut — существует committed snapshot ∧ journal цел → откат к нему |
| `Provisioning → Unprovisioned` (corruption) | повреждение journal (CRC структуры/старой записи) → defaults + Degraded (HZ-14), обязателен re-provisioning |
| `Provisioned → Provisioning` | re-provisioning (смена номера или запись persisted-конфигурации): admission — слот свободен (молчаливых терминаций нет), stationary — деталь flow (#50) |

Guard перехода провала — предикат «существует валидный committed snapshot ∧ journal цел» — host-проверяем.

**Калибровка — вне оси.** `Calibrate` — root-операция (motion, эксклюзив, admission: окно Serving, health Ready **или Degraded** (не Fault — fault-state gate по канон. доку #40; отказ энкодера ловится внутри операции — `EncoderReadFault`), provisioning Provisioned, в канале; алгоритм — #40); результат — таблицы сегментов в буферах конфигурации. `PersistConfiguration` — сервисная операция в покое (quiescence, C6), ось provisioning не меняет. Отдельный **calibration-validity gate**: persisted-таблицы + CRC/plausibility; невалидны ⇒ Degraded (ограничение positioning-зависимых capability), гейт `Provisioned` не блокирует. **Восстановление таблиц из flash после reboot — открытая зависимость U04 (V1 закомментировано), решается на gate контракта Calibrate (#40), не здесь.**

## 4. Operation lifecycle (решён #13, фиксируется)

```text
Accepted → Running → Succeeded
Running → Stopping → Cancelled
Running → Failed
Stopping → Failed
```

- `Rejected` — результат admission запроса, не состояние экземпляра.
- Root-экземпляры образуют конечное дерево владения (parent/child, без циклов); suboperation — контекстная роль, primitive — без lifecycle.
- Проекция `Idle/Running`: нет активного root-экземпляра / есть хотя бы один.
- Terminal outcome (typed code + диагностический контекст) неизменяем повторной доставкой.
- Взаимодействие: admission (окно → health → provisioning → эксклюзив → preconditions типа) — раздел 8; latched fault переводит активные экземпляры в `Stopping/Failed` по правилам #45/#13 (safety precedence не нарушает identity и traceability).

## 5. Health-ось (решена #45, фиксируется)

`Initializing → Ready ↔ Degraded → Fault`; Degraded — потолок скорости ≤ 1.0 м/с (F5), T_deg — открытый вопрос #48. Recovery: auto-clear eligible (ToF/AS5600/I2C, квалификация + stationary), qualified-clear (low battery), explicit reset + persisted marker (Backup SRAM; power-cycle ≠ acknowledgment); restart reset-cause-aware (marker → `Fault`, иначе `Initializing → Ready/Degraded` после grace + requalification). Взаимодействия с другими осями: раздел 10.

## 6. Session-ось (Manual Control Session; владелец: Manual Session)

```text
Closed → Opening → Active → Closing → Closed
Opening → Closed        (admission-отказ)
Active → Closing        (lease expiry / link loss / явное закрытие / stop intent / latched fault)
```

- **Admission входа/выхода сессии — Semantic Contract & Admission** (закрытие C9 ревью #43): старт — окно Serving, health ∈ {Ready, Degraded}, provisioning Provisioned, слот свободен, authority Control Client. `Opening → Closed` при отказе.
- **Active**: lease свеж — hold-to-run intents продлевают; stale sequence отклоняются (детали #47); Manual Session — единственный писатель lease-счётчиков и expiry-события; Safety Authority — единственный потребитель: expiry → stop-intent через воронку (INV-LEASE-STOP, CONTROLLED).
- **Closing**: hold-to-run отклоняется; bounded stop в полёте; `Closed` после завершения stop и освобождения слота. Latched fault → `Closing` (safety stop уже в воронке).
- Сессия не переживает reboot и Fault (новая авторизация — новая сессия).
- **Recovery-jog** — та же ось, параметр политики `normal | recovery-jog`, неизменяем на время сессии: start guard — окно Serving, health **Fault**, provisioning Provisioned, **авторизация Safety Authority** (intent через воронку как safety-intent — иначе INV-FAULT-ADMISSION отклоняет), границы скорость/дистанция/направление (item 4), слот занимает. Сессия ≠ операция: отдельный lease-контракт (#13), `operationId` не выдаётся.

## 7. Update-ось (владелец: Update Authority + Persistence adapter)

```text
Idle → Staging → Applying → Committed | RolledBack | Failed
Staging → Aborted            (abort / отказ валидации / latched fault)
Aborted → Idle               (по завершении handoff окна либо при старте нового update)
Failed → Recovering → Committed | Failed
```

- **Aborted** - терминал отмены staging: abort пользователя, отказ валидации, **latched fault**; active image не тронут, marker не установлен, staging-область отбрасывается по собственной CRC. Power-cut во время `Staging` - reboot: ось стартует в `Idle` (runtime-состояние, маркера нет). **Handoff окна: `Update → Serving` при update-оси ∈ {`Committed`, `Aborted`} и при совместимом `RolledBack`; несовместимый `RolledBack → Recovery` по #50 §3.3/§11** - зависшей комбинации `window=Update, update=Idle` не существует. При Aborted по fault: окно `Serving`, health `Fault` - движение закрыто INV-FAULT-ADMISSION, read-only диагностика доступна. Host-тест обязателен: `Staging + latched fault → Aborted → окно Serving`.

- **Idle**: обновлений нет.
- **Staging**: образ принимается по Update-классу очередей (reserved capacity для in-progress, reject для новых — #43), валидируется (checksum + signature; детали #47/#50), **bulk-запись образа — в staging-область (здесь сосредоточены длинные flash-окна)**. Abort / отказ валидации / **latched fault** → `Aborted` (marker не установлен ∧ active image не тронут; staging-область отбрасывается по собственной CRC; окно → `Serving`, handoff по §7-Aborted); power-cut → reboot в `Idle`. Вход в `Applying` дополнительно требует ¬Fault. Весь lifecycle — окно `Update`: motion закрыт, слот занят (mutating maintenance, issue 2), read-only диагностика разрешена (транспортные адаптеры допускают drop/RX-overflow в пределах bounded budgets — значения #48; клиенты реконсилируются query/snapshot).
- **Applying** (активация): вход — quiescence подтверждена (C6: stationary по snapshot'ам Actuator/Safety, W_flash < safety-дедлайна ∧ < watchdog; single-bank F405: erase ≤ 4 s, весь flash stalled, ISR deferred ≤ 4 s), **маркер активации — flash-резидентный** (владелец Persistence adapter; Backup SRAM вольтилен при power-cut — иначе разрыв set-marker → активация не детектируется, HZ-15/proving #11), активация образа и перезапуск (конкретная механика активации — #50). Power-cut в `Applying` → boot с маркером → окно `Recovery`. **Контракт окна: Applying обязан иметь измеренный bound `W_apply` (источник — #48/proving slice) и явный safety handling: прерывание update только на границах окон (abort-before-activation на входе Applying); bumper внутри окна — latch + post-window обработка (Q7.2, принятый риск). Секторная раскладка и длительность — #50/#48, здесь не фиксируются.**
- **Commit/rollback на первом старте после активации**: критерий - **целостность образа (CRC/signature) + ограниченное число попыток старта** (bounded boot attempts; ловит bootloop «загрузился, но умирает позже»). **Health-ось и crash-маркер (#45) в критерий не входят** - INV-STARTUP-GATE это motion-gate, а присутствие persisted-marker означает Fault-стартап независимо от образа. Прошёл → auto-commit (`Committed` → Idle, окно Serving); исчерпаны попытки → auto-rollback к предыдущему image (`RolledBack` → Idle), затем membership `configuredProfileId ∈ qualifiedProfileIds` загруженного image определяет окно: совместим → `Serving`, несовместим → `Recovery` + `ProfileQualificationMismatch`, active profile отсутствует и motion закрыт (#50 §3.3/§11). Ручной откат - service-команда (поверхность #50), отдельного состояния не вводит. Точная механика (проверка целостности, счётчик попыток, boot-stage) - **#50**.
- **Failed**: rollback невозможен / образ повреждён → окно `Recovery`, health Degraded (HZ-15), ограниченная Service-поверхность.
- **Recovering**: recovery-перепрошивка через Service-поверхность; успех → `Committed` → Idle, окно Serving; провал → `Failed`, retry.
- **Аппаратный факт владельца (вход для #50):** использование BOOT/RES пинов запрещено разработчиками PCB; софтовый reset в ROM-UART-bootloader может давать сбросы и помехи от CAN/I2C — **вероятно потребуется собственный bootloader** (boot/update/recovery firmware, issue 2); feasibility и решение механики — **#50** (владелец update-механики). Power-cut mid-update — обязательный тест proving slice (#52/#54).

## 8. Exclusive Control Activity и admission-матрица

Один эксклюзивный слот (I-LC-4); занятый слот → `ResourceConflict`; порядок проверок: окно → health → provisioning → эксклюзив → preconditions типа (#13). Safety intents не гейтятся и никогда не отклоняются (воронка, #45). Каждый класс разбит на start guard (admission) и active invariant.

| Класс | Start guard | Active invariant |
| --- | --- | --- |
| Motion-операция (auto, root) | Serving, Ready/Degraded (capability-ограничения, ≤1.0 м/с в Degraded), Provisioned, слот свободен | слот занят; окно Serving; Fault → stop по #45; terminal outcome → слот освобождён |
| Manual-сессия (normal) | Serving, Ready/Degraded, Provisioned, слот свободен | слот занят; INV-LEASE-STOP |
| Recovery-jog | Serving, Fault, Provisioned, авторизация Safety Authority, слот свободен | слот занят (сессия); границы item 4 |
| Mutating service (provisioning, PersistConfiguration) | Serving, Ready/Degraded (иначе HZ-14 необратим), слот свободен | слот занят; может менять ось provisioning |
| Calibrate | Serving, Ready **или Degraded** (не Fault — fault-state gate по канон. доку #40; Degraded-потолок ≤1.0 м/с не блокирует калибровочную скорость; отказ энкодера ловится внутри операции — `EncoderReadFault`), Provisioned, слот свободен, в канале | слот занят; fault-пути #40 |
| Update | **Serving**, Ready/Degraded, Provisioned, слот свободен | handoff: окно → Update; слот занят; завершение → Serving/Recovery |
| Query / read-only / подписки | Serving/Update/Recovery (не Boot), health любое (вкл. Fault), слот не требуется | окно не меняют; bounded budgets (#48) |
| Safety intents (stop / force-stop / evacuation) | не гейтятся | приоритет над слотом; stop-профиль по #45 |

## 9. Restart-таблица (что переживает reboot)

| Сущность | Переживает reboot | Механика |
| --- | --- | --- |
| Платформенное окно | нет → всегда `Boot → Serving` | runtime |
| Health | условно: crash-class marker → `Fault` (reconciliation reset-cause); иначе `Initializing → Ready/Degraded` | Backup SRAM: marker + breadcrumbs + crash counter (#45 Q5 A) |
| Provisioning (identity + конфигурация) | да, атомарно: power-cut mid-write → rollback к committed; повреждение journal → Unprovisioned + Degraded | journal + CRC (HZ-14) |
| Калибровка (persisted-таблицы) | **открытая зависимость U04 → gate контракта Calibrate (#40)**; требование восстановления из flash — пункт #43 | Config & Profile |
| Operation instances | нет — умирают с epoch; snapshot нового epoch не содержит чужих активных операций | epoch fencing (#13) |
| Idempotency ledger | нет — bounded in-memory (#13); клиент обязан re-handshake | epoch |
| Manual-сессия | нет — завершается (reboot = stop) | runtime |
| Update | marker: staging → discard → Idle (reboot: ось стартует в Idle; в рамках boot-instance отмена staging — терминал `Aborted`); applying → Recovery/rollback/commit на первом старте; **recovering → Boot → Recovery (маркер сохранился), запись возобновляется с CRC-проверки, активный образ частично не травмируется; повторная порча recovery-области → стойкий Failed + Degraded** | persisted marker |
| Wall clock (RTC) | да (детали питания — #50) | RTC adapter |
| Backup SRAM (markers, breadcrumbs) | да | Persistence adapter (#43) |

Новый epoch выдаётся при выходе из `Boot`; стартап всегда reset-cause-aware (watchdog / HardFault / power-on / marker — #45).

## 10. Взаимодействия осей (детерминизм и edge-cases)

| Комбинация | Правило |
| --- | --- |
| Fault в любой момент | активные operations → `Stopping/Failed`; сессия → `Closing`; update в `Staging` → discard stage-области → `Aborted`, окно `Update → Serving` (health Fault — motion закрыт, read-only доступна; активный образ не тронут); update в `Applying` невозможен с Fault (quiescence требует Ready/Degraded, вход — ¬Fault); **для остальных классов окно не меняется** |
| Fault во время `Provisioning` | текущий bounded-этап записи завершается атомарно до границы шага; переход оси определяется только journal (CRC/committed snapshot), не health-осью; health обрабатывается отдельно; движение остаётся закрытым |
| Degraded при активной сессии | сессия продолжается, движение ограничено потолком ≤ 1.0 м/с + capability-ограничениями; stop-профили по #45 |
| Update при активной сессии/операции | admission: слот занят → `ResourceConflict`; молчаливых терминаций нет |
| Provisioning при `Unprovisioned + Fault` | provisioning-операции (не motion) допустимы (иначе HZ-14/13 необратимы); движение — нет |
| Read-only при `Update`/`Recovery` | разрешена, bounded бюджетами (#48); не занимает слот; в окне Update транспортные адаптеры допускают drop/RX-overflow в пределах budgets, клиенты реконсилируются query/snapshot |
| Jog в Fault vs evacuation | jog — сессия (слот, границы item 4); evacuation — safety intent без слота, приоритетнее (воронка) |
| Safety intents при Applying | атомарное окно активации не прерывается mid-write: bumper — latch + post-window (Q7.2); прерывание update — на границах окон (abort-before-activation на входе Applying); `W_apply` — измеренный bound (#48) |
| Epoch смена | persisted-оси не зависят от epoch; runtime-сущности умирают; клиент re-handshake (#13) |
| Flash-окно (≤ 4 s, single-bank) | Applying только при quiescence (C6: W_flash < safety-дедлайна ∧ < watchdog); bumper в окне — latch + post-window обработка (#45 Q7.2); watchdog окно = max(шаг, flash, ISR) + margin (#43) |

## 11. Открытые зависимости (не решаются здесь)

- Численные значения (T_lease, окна квалификации, budgets, watchdog-формула) — #48 (measurement-resolved, источник у каждого числа).
- Механика update: транспорт, slot-раскладка, собственный bootloader (BOOT/RES пины запрещены PCB-разработчиками, ROM-UART-загрузка ненадёжна), boot-attempt counter, ручной откат, RTC-питание, секторная раскладка окна Applying — #50; измеренный `W_apply` — #48/proving slice.
- Восстановление калибровки из flash (U04) — gate контракта Calibrate (#40).
- Реализуемость force-stop кадра — проверка F4 (#45 → #54/#50).
- Wire-кодирование rejection codes, session sequence/ACK, update-транспорт — #47.
- Calibration-validity gate (CRC/plausibility persisted-таблиц) — детальные пороги и классы ограничения — #48/#40.

## 12. Ссылки

- Тикет «Спроектировать lifecycle-оси и переходы V3» (#46) и его resolution.
- #2 (системный контекст, оси, Exclusive Control Activity), #13 (semantic contract), #43 (границы и владельцы), #45 (safety-модель), #8 (items 5/6/7, gates), #10 (execution architecture).
- `CONTEXT.md`, `docs/safety-model-v3.md`, `docs/software-architecture-boundaries-v3.md`, `docs/semantic-contract-v3.md`, `docs/calibrate-algorithmic-documentation-v3.md`.
