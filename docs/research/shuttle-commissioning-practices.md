<!-- markdownlint-disable MD013 MD060 MD024 MD036 -->

# Индустриальные практики приёмки паллетных шаттлов и transfer-car (стенд / полигон / приёмка)

## Evidence metadata

- **Source**: внешние первичные/вторичные источники, собранные по сети (agent-reach: Exa search + Jina Reader) 2026-09-01; репо-входы `docs/safety-model-v3.md`, `docs/verification-strategy-v3.md` (§7.2/§7.4), `docs/research/v1-actuation-inventory.md`, `docs/research/v1-sensor-io-facts.md`.
- **Version**: revision 1, research-тикет [#106](https://github.com/Driadix/ShuttleControllerV3/issues/106) карты [#103](https://github.com/Driadix/ShuttleControllerV3/issues/103).
- **Confidence**: каждый вывод снабжён ссылкой. Стандарты (ISO 13849-2, ISO 3691-4, EN 528, VDI 2710) цитируются по публичным previews/официальным описаниям — полные тексты платные, дословные значения из них не воспроизводятся (пометка `[preview]`). VESC-факты — по официальной документации репозитория `vedderb/bldc` и forum-высказываниям автора firmware (Benjamin Vedder) — класс `primary-docs`. Вендорские практики (Dematic/Daifuku/SSI Schäfer/Swisslog/E&K-ek robotics/Dexion/CONTREX/ZAPI/MiR/arculus/mayr) — по их публичным материалам, класс `vendor`.
- **Scope**: практики, а не сертификация — соответствие парадигме non-certified проекта (#52: «Полный ISO 26262-6 work-product набор отклонён»). Стандарты используются как источник шаблонов.

## 0. Резюме

Индустрия устойчиво делит проверки паллетных шаттлов / AGV / transfer-car на три зоны, и деление совпадает с нашей пирамидой L4/L5/field (#52 §2) почти один-в-один:

1. **Стенд/фабрика (наш L4/L5)**: I/O-проверки, протокольная целостность, fault-injection safety-функций, эмуляция окружения (интеграторы поголовно держат протокольные эмуляторы: Savant VPath, Emulogix, WAMAS Emulation Center, Daifuku Sym3, Swisslog SynQ). Ничего физического.
2. **Полигон/первый заход на объект (наш field до приёмки)**: физика привода и торможения при реальной нагрузке (brake-тест по методике MiR: полная нагрузка, худшая точка, полная скорость, замер дистанции), фактическая реакция привода на команды и потерю команд, направление, калибровки.
3. **Приёмка на объекте (наш commissioning/ОСТ)**: геометрия и стеллаж, нагрузочные тесты (EN 528 §5.4 static/dynamic load tests), интегрированный commissioning (EN 528 §6.3.6), availability/running-in (FEM 9.222), per-device fail-safe-тест перед вводом.

Ключевые переносы для карты #103:

- **duty0-реакция VESC**: в FOC duty-режиме duty ниже back-EMF = активное торможение, не выбег; фактический «сейф-стоп» привода = drive CAN-timeout (default 0.5 s) + timeout brake current (default 0 = выбег). Это надо мерить на полигоне — прямой вход в T5-чек-лист (#108) и в решение F4 (#113).
- **Brake-тест**: индустриальный шаблон (MiR ≥ 20 см до препятствия при полной нагрузке/скорости; arculus с нагрузкой и без; ZAPI в обоих направлениях с нагрузкой и без) — готовая методика для D_brake (#52 §5.2) и INV-BRAKE-VALIDITY.
- **Сенсорика**: Dexion-класс шаттлов подтверждает наш паттерн (4 парных позиционных датчика, концевики лифта, датчик наличия рельса, мониторинг вращения отдельным сенсором) и добавляет индустриальную конвенцию: safety-входы NC (fail-safe на обрыв), дискреты проверяются в обоих состояниях при приёмке с записью part number / NPN-PNP / NO-NC / питания.
- **HIL**: реальный паттерн интеграторов — протокольный эмулятор как CAN/PLC-узел, который «отвечает на выходы контроллера и возвращает осмысленные входы» (Emulogix); ровно это планируется как T3-эмулятор VESC-subset (#109).

## 1. Как индустрия делит проверки «стенд / полигон / приёмка»

### 1.1 Нормативная рамка (шаблоны, не сертификат)

| Документ | Что задаёт | Источник |
| --- | --- | --- |
| **VDI 2710 Blatt 5** «Acceptance specification for automated guided vehicle systems (AGVS)» | Структура приёмки AGVS: vehicle acceptance → functional test subsystems → automatic test subsystems → test run overall plant → performance test → availability test; протоколы и чек-листы (скорости, availability, Leistung) как обязательные приложения; техаспекты фиксируются ещё в заказе | [VDI 2710-5 TOC preview (PDF)](https://www.vdi.de/fileadmin/pages/vdi_de/redakteure/richtlinien/inhaltsverzeichnisse/2074815.pdf), [Intertek card](https://inus04aapb1h3nprod.dxcloud.episerver.net/en-us/standards/vdi-2710-blatt-5-2013-12-1116083_saig_vdi_vdi_2592125/) `[preview]` |
| **FEM 9.222** «Acceptance and availability of installations with S/R machines» | Порядок: preliminary/intermediate inspections → partial hand-overs → official inspection → acceptance; три вида доказательств — **function test** (функции без метрик), **performance test** (против измеримой спецификации, ссылка на FEM 9.851 cycle times), **availability test** (статистическая модель, время наблюдения фиксируется в договоре); после приёмки — **running-in operation** и measurement phase | [FEM 9.222 (PDF)](https://www.fem-eur.com/wp-content/uploads/2016/03/FEM-9_222_Englisch.pdf) |
| **EN 528:2021** (rail dependent S/R machines; satellite vehicles = шаттлы включены как load-handling-device) | §5.4 load tests: test load, static load test, dynamic load test, load test record; §6.2 erection on site; §6.3 commissioning: method statement, instructions, training, safety devices, **individual elements**, **integrated commissioning**; §6.4.7 test report; §6.7 maintenance/inspection/testing | [SIST EN 528:2021 preview](https://cdn.standards.iteh.ai/samples/68054/a458e1b2fdc84094b2612d96abba0ce5/SIST-EN-528-2021.pdf) `[preview]`, [genorma card](https://genorma.com/en/standards/en-528-2021-a1-2022) |
| **ISO 3691-4:2023** (driverless industrial trucks) | Триада verification: type testing (Test A & B — brake performance), functional verification, **commissioning**; personnel detection = «stop before contact» (не «generate a signal» как в EN 1525) ⇒ обязывает оценивать всю цепочку детекция→торможение; braking system PLr d по ISO 13849-1 | [TÜV Rheinland whitepaper (PDF)](https://www.tuv.com/content-media-files/master-content/services/industrial-services/pdf/tuv-rheinland-automatic-guided-vehicles-whitepaper-en_neu.pdf), [ISO card](https://www.iso.org/standard/83545.html) `[preview]` |
| **ISO 13849-2:2012** (validation SRP/CS) | Валидация = analysis + functional tests по validation plan; fault-injection когда анализ неконклюзивен; test records: кто, условия среды, процедуры/оборудование, дата, результат; валидатор независим от разработчика | [ISO 13849-2 preview (PDF)](https://cdn.standards.iteh.ai/samples/53640/e240593c209440729727cd6ec30d7a94/ISO-13849-2-2012.pdf) `[preview]` |
| **ISO 12100:2010** | Общая рамка: risk assessment по lifecycle-фазам, включая commissioning; документирование и верификация мер | [ISO card](https://www.iso.org/standard/51528.html) `[preview]` |

### 1.2 Что индустрия выносит на стенд (до объекта)

- **I/O и контуры** проверяются до того, как «линия двинет первый tote»: loop checks, калибровки, верификация сенсоров и safety-цепей — на скамье, не на пике ([merxima commissioning](https://merixa.com/services/commissioning)).
- **FAT = скриптованные тесты против симулированного host и представительных нагрузок**: механическая повторяемость, motion-профили, исполнение safety-функций под симулированными отказами, последовательности WCS, end-to-end заказ; измеренные cycle times и error rates ([SmartLoadingHub FAT/SAT checklist](https://www.smartloadinghub.com/insights/warehouse-robotics/asrs-decision-matrix-fat-sat-checklist/)).
- **Fault-injection на каждый критичный I/O**: test-векторы normal / fail-open / fail-short / noisy; проверка watchdog-таймеров и диагностических кодов ([SmartLoadingHub, там же](https://www.smartloadinghub.com/insights/warehouse-robotics/asrs-decision-matrix-fat-sat-checklist/)); в нормативной логике — ISO 13849-2 (fault-injection как дополнение анализа, `[preview]`).
- **Engineering shakedown перед формальной qualification**: dry-run всех test cases, worst-case стресс до формальных тестов — практика CQV-агентств на ASRS ([Performance Validation, кейс Dematic ASRS](https://perfval.com/case-study/dematic-automated-storage-and-retrieval-system-validation/)).
- **Протокольная эмуляция вместо железа**: интеграторы тестируют реальный управляющий софт против эмулятора машин (раздел 5 ниже) — физики на стенде нет.
- **Практика «FAT — про целостность железа, SAT — про реальный процесс»**: «excessive shop testing beyond basic I/O verification can be counterproductive... Focus FAT on hardware integrity and basic functionality, reserving comprehensive process testing for SAT» ([Industrial Monitor Direct, FAT/SAT procedures](https://industrialmonitordirect.com/blogs/knowledgebase/fat-and-sat-procedure-guidelines-for-industrial-control-systems)).

### 1.3 Что всегда остаётся фактом на объекте / полигоне

- **Brake / stopping distance при реальной нагрузке** — не эмулируется ни у кого: «The braking system must be able to bring the vehicle to a safe stop within the sensor detection range even under adverse conditions such as maximum rated load or on a downhill slope» ([mayr, AGV safety brakes](https://www.mayr.com/en/blog/more-than-just-braking~54418)); MiR-комиссинг: brake-тест полным ходом на худшей точке с максимальной нагрузкой, порог ≥ 20 см ([MiR500 Commissioning Guide](https://all-guidesbox.com/manual/1714203/mir-mir500-commissioning-manual-13.html)); ISO 3691-4 — type testing Test A/B как verification при вводе ([TÜV whitepaper](https://www.tuv.com/content-media-files/master-content/services/industrial-services/pdf/tuv-rheinland-automatic-guided-vehicles-whitepaper-en_neu.pdf)).
- **Нагрузочные тесты машин** (static/dynamic load test + запись) — при монтаже, EN 528 §5.4 `[preview]`.
- **Геометрия объекта и механика**: Dexion — при первом старте проверяется правильность сборки end-stoppers и стеллажа ([Dexion/Constructor Pallet Shuttle User Manual, §5.3/§5.8](https://sec-group.co.uk/wp-content/uploads/2025/01/EN-DEXION_CONSTRUCTOR-PALLET-SHUTTLE-SYSTEM-User-Manual-Issue-26.03.2014.pdf)); позиционирующие колёса регулируются производителем «according to the channeled storage at the moment of the startup» (там же, §2.4.2) — настройка per-site.
- **Зоны детекции под живой трафик**: «scanner may detect obstacles correctly, yet still create unsafe behavior if conveyor release timing is wrong» — проверки в реальной среде, не только по документации ([sectorreviewhq, ISO 3691 checklist for AGV site acceptance](https://www.sectorreviewhq.com/news/Forklifts_Handling_Vehicles/AGV_Tow_Tractors/ISO_3691_Checklist_for_AGV_Safety_Audits_What_to_Review_Before_Site_Acceptance.html)).
- **Порядок подъёма скорости**: «slow full cycles with real product in real conditions, then speed raised in stages... Resist the pressure to jump straight to full speed because the FAT passed» ([Path2 Robotics, robot cell commissioning](https://path2.io/resources/robot-cell-commissioning-guide)).
- **Интегрированный commissioning и running-in**: EN 528 §6.3.6 integrated commissioning `[preview]`; FEM 9.222 §5.1 running-in («stabilize the system technology... sacrifices in performance and reduced availability may occur»).

### 1.4 Сопоставление с нашей пирамидой

| Индустрия | Наш уровень (#52 §2) | Комментарий |
| --- | --- | --- |
| FAT: скрипты против эмулятора, I/O fault-injection | **L5 HIL** (стенд #103) | Совпадает: обязательные 6 acceptance + as-needed fault-injection (#52 §6.2/6.3) |
| FAT: базовая целостность железа | **L4 bench** (плата без механики) | Аналог bring-up + adapter bounds |
| Полигон/первый заход: физика привода, brake, направление | **field** (implementation-карта, «полигон — вторая линия уверенности», решение владельца #103) | Совпадает: физика приводов — не стенд (#103 Notes: «Реальный привод на стенде не эмулируется») |
| SAT/приёмка: геометрия, нагрузочные, integrated commissioning, availability | **commissioning Q7.1 A + полевые obligations** (#52 §5.2, safety-model §6.1) | Совпадает: per-device fail-safe-тест перед входом в Ready = индустриальный commissioning-тест |

Вывод: деление карты #103 («стенд = протокольный эмулятор, физика = полигон, приёмка = per-device commissioning») — мейнстрим, а не компромисс. Прямых контрпримеров (вендор, эмулирующий торможение на стенде ради приёмки) в источниках нет.

## 2. Safe commissioning приводов (duty0-реакция, timeout, brake)

### 2.1 Факты VESC (наш привод FSESC75100, #104)

| Факт | Значение | Источник |
| --- | --- | --- |
| **CAN-timeout привода** | По умолчанию мотор останавливается, если на CAN не поступало команд > 0.5 s; timeout настраивается; 0 = отключён («strongly recommended to not disable in production») | [vedderb/bldc `documentation/comm_can.md`](https://github.com/vedderb/bldc/blob/master/documentation/comm_can.md) |
| **Timeout brake current** | По timeout прикладывается настраиваемый тормозной ток; **по умолчанию 0 = просто «releases the motor»** (выбег) | там же |
| **Keep-alive** | Команды рекомендуется слать постоянно на фиксированной частоте, ~50 Hz | там же |
| **Duty-режим в FOC** | «FOC... duty cycle mode will always cause braking when setting a value lower than the back emf» — duty ниже back-EMF = активное торможение (синхронная ректификация), не выбег | [vesc-project.com node/134 (Benjamin Vedder)](https://vesc-project.com/node/134) |
| **Вне-диапазонные команды** | Клампятся к лимиту, не отвергаются («sent with 60A the firmware will use 50A») | [comm_can.md](https://github.com/vedderb/bldc/blob/master/documentation/comm_can.md) |
| **Формат кадра** | Extended ID 29 бит: command в битах 8–15, controller_id в битах 0–7; SET_DUTY = cmd 0, scaling 100000 | [comm_can.md](https://github.com/vedderb/bldc/blob/master/documentation/comm_can.md), [VESC6 CAN Formats (PDF)](https://vesc-project.com/sites/default/files/imce/u15301/VESC6_CAN_CommandsTelemetry.pdf) |
| **Статусный кадр CAN_PACKET_STATUS (9)** | ERPM (4 Б), current*10 (2 Б), duty*1000 (2 Б) — ток в байтах 4–5 с единицей 0.1 A | [OpenRobot, Control with CAN](https://dongilc.gitbook.io/openrobot-inc/tutorials/control-with-can), [VESC6 CAN Formats (PDF)](https://vesc-project.com/sites/default/files/imce/u15301/VESC6_CAN_CommandsTelemetry.pdf) — подтверждает интерпретацию V1 «2405 = ток, raw 500 = 50 A» (#104, [v1-actuation-inventory §2](./v1-actuation-inventory.md)) |
| **Безопасный первый пуск** | «Wheels off the ground, propellers disassembled, fingers out of the way?»; старт с заниженных лимитов тока (15–20 A вместо 40 A); проверка направления + галка Inverted при обратном вращении | [vesc-project.com Motor Wizard FOC node/938](https://vesc-project.com/node/938) |
| **Температурный headroom для тормоза** | Acceleration Temperature Decrease: cutoff разгона на 15% ниже cutoff температуры — «safety temperature headroom for the brakes» (торможение сохраняется при перегреве разгона) | [vesc-project.com node/183](https://vesc-project.com/node/183) |

**Следствие для F4 (#113):** VESC-native «сейф-стоп» = (а) прекращение команд → drive-timeout (0.5 s default) → стоп; (б) timeout brake current (0 = выбег); (в) duty0-кадр = активное торможение в FOC, но это командный путь, а не аппаратный приоритет. Скоростной приоритет кадра по min-ID в VESC-протоколе не существует как native-команда (набор команд: SET_DUTY/SET_CURRENT/SET_CURRENT_BRAKE/...) — это и есть открытый вопрос F4. Исследовательский вывод (не решение): индустриальный эквивалент verified-fail-safe — измерить фактическую timeout-реакцию каждого привода и считать её базовым сейф-стопом, что совпадает с commissioning-тестом Q7.1 A (safety-model §6.1: «прекращение команд → наблюдаемое timeout/brake поведение»).

### 2.2 Индустриальные шаблоны commissioning приводов

- **ZAPI COMBI AC0** (промышленные контроллеры погрузчиков) — канонический порядок: проверка проводки → acquisition сигнала → максимальный ток → **«Test the parameters in both directions»** → creep-частота (машина «should just move» при закрытом микровыключателе) → cutback-скорости → **release braking на полной скорости** → **inversion braking: сначала 25% скорости, потом полная; для погрузчика повторять с нагрузкой и без; «the unloaded full speed condition should be the most representative condition»** → max speed F/R → **тест на максимальной спецификации рампы с полной нагрузкой** → поведение на уклоне. Диагностика в 4 режимах (startup/standby/driving/continuous): watchdog, current sensors, CAN-bus interface ([ZAPI COMBIAC0 manual, PDF](https://dc66.ru/upload/iblock/6c8/cdx95fc18wlcn9n59xysqdqp593uugmn.pdf)).
- **MiR500 Commissioning Guide §3.5 Brake test**: (1) максимальная нагрузка, с которой робот будет ездить; (2) худшая точка (уклон); (3) полный ход джойстиком к объекту (картонная коробка); (4) замер дистанции от робота до объекта; **порог ≥ 20 см = тест пройден** ([MiR500 Commissioning Guide](https://all-guidesbox.com/manual/1714203/mir-mir500-commissioning-manual-13.html)).
- **arculus (AMR-производитель)**: commissioning = калибровка сканеров → калибровка приводов и лифта → dry runs → прошивка safety-конфигурации и release → **brake tests с нагрузкой и без** → performance-тест под сценарий заказчика ([arculus, Commissioning AMRs](https://www.arculus.de/commissioning-amrs)).
- **mayr (производитель AGV safety-brakes)**: fail-safe пружинные тормоза (обесточен = закрыт; работают при e-stop, потере питания, обрыве провода); полная остановка = сумма задержек сенсорики+контроллера+контактора и **t1 switching time** тормоза — время нарастания момента; «select brakes with the shortest possible, verified switching times»; мониторинг состояния тормоза (ROBA brake-checker: износ, времена переключения по кривым тока катушки, «preventative function monitoring») ([mayr blog](https://www.mayr.com/en/blog/more-than-just-braking~54418), [ROBA brake-checker](https://www.mayr.com/en/products/safety-brakes/supply-and-monitoring-components/roba-brake-checker~2134)).
- **CONTREX M-Shuttle** (серво-система шаттл-класса): старт в Direct Mode с setpoint; **проверка полярности энкодера по знаку скорости** (если минус — поменять линии); при несовпадении направления follower/lead — «rewire the drive/motor to reverse the motor direction»; Tune Mode — автоматическое движение между лимитами на jog-скорости ([CONTREX M-Shuttle Installation/Start-up Guide, PDF](https://www.contrexinc.com/PDF/UserGuides/M-ShuttleInstallGuide.pdf)).
- **INVT Goodrive20-09 (hoisting VFD)**: для подъёмных применений — **torque verification перед отпусканием тормоза** (ток/момент выше порога фиксированное время, иначе fault «verification fault»), brake feedback, zero position detection ([INVT GD20-09 manual, PDF](https://www.invt.com/uploads/file1/20250902/GD20-09%20VFD%20Manual_V1.1.pdf)). Паттерн релевантен лифтеру (HZ-07/08): момент удержания подтверждается до снятия механического тормоза/до движения с грузом.

### 2.3 Перенос в чек-лист T5 (сайт-факты, #108) и сценарную матрицу (T1 #107 / T4 #113)

> Замечание о нумерации: в разных тикетах карты T-метки расходятся (в теле #106 сайт-факты названы T4, в #113 «первый заход на полигон» = T4). Здесь используется нумерация из assignment: **T5 = сайт-чек-лист (операционализирован #108), T1 = сценарная матрица (#107), T4 = полигонный заход force-stop (#113/F4)**.

**Раздел «Привод: реакция и fail-safe» для T5 (#108)** — каждый пункт с «как измерить / что зафиксировать»:

1. **Фактический drive CAN-timeout**: при вывешенных колесах (wheels off ground — VESC-практика node/938) подать команду движения, затем прекратить все команды; замерить время от последнего кадра до остановки. Ожидание: ~0.5 s default. Зафиксировать: значение, изменено ли с default.
2. **Timeout brake current**: замерить поведение после timeout — выбег (0) или торможение; зафиксировать сконфигурированное значение.
3. **Реакция на duty0-кадр** при вращении (первый шаг F4, как в #108 body): замедление с замером (FOC: активное торможение, vesc-project node/134) vs выбег vs игнор. Зафиксировать: характер, оценочное замедление.
4. **Направление**: команда «вперёд» (spd>0, без inverse) → физическое направление колеса; сверить с семантикой motorReverse^inverse V1 (v1-actuation §1). Практика: CONTREX polarity-check по знаку; VESC Inverted-галка.
5. **Статусные кадры**: какие CAN ID реально эмиттируются, rate, состав; подтвердить units тока (0.1 A → порог 500 raw = 50 A) — вход для #104/#109.
6. **Клампинг вне-диапазонных команд** (для матрицы T1, стенд): команда выше лимита не отвергается, а усекается (comm_can.md) — контракт эмулятора T3 должен это отражать.
7. **Токовые лимиты привода**: motor current max / max brake — зафиксировать фактические значения (VESC Tool / по кадрам CONF).

**Раздел «Brake» для T5/T4**:

1. **Brake-тест по MiR-методике** (адаптация: уклон в стеллаже невозможен — худшая точка = полная нагрузка + максимальная скорость профиля): полный ход к препятствию (картон/буфер), замер дистанции остановки, повтор с нагрузкой и без (arculus, ZAPI: «with and without load»), в обоих направлениях (ZAPI). Это готовая методика для **D_brake(v, load)** (#52 §5.2) и выхода из U10 (стоп-паттерн 10×100 мс).
2. **Периодический ре-тест fail-safe** — в план ТО как INV-BRAKE-VALIDITY (safety-model §3): индустриальный аналог — mayr brake-checker (мониторинг времени переключения/износа) и ZAPI continuous diagnosis; у нас — ручной ре-тест по плану ТО (safety-model HZ-03: «периодический ручной ре-тест fail-safe»).

**Матрица T1/T4** (кто что проверяет):

| Практика | Среда | Источник-шаблон |
| --- | --- | --- |
| Контракт команд (клампинг, формат, keep-alive 50 Hz) | T3 стенд (эмулятор) | comm_can.md |
| Force-stop арбитраж (min-ID vs duty0) | T3 стенд + T4 полигон | #113 (решение владельца) |
| Фактический timeout / timeout brake / duty0-реакция | T4 полигон (первый заход) | comm_can.md, node/134 |
| D_brake под нагрузкой | T4 полигон (measurement record) | MiR §3.5, mayr |
| Направление, полярности | T4/T5 | CONTREX, VESC node/938 |
| Torque/ток удержания лифтера | T4 полигон | INVT torque verification |
| Per-device fail-safe-тест перед Ready | приёмка (Q7.1 A) | safety-model §6.1 = ZAPI/MiR-класс практики |

## 3. Индустриальные паттерны сенсорики (bumper / pallet / концевики / каналы)

### 3.1 Что стоит на реальных паллетных шаттлах (ближайший класс — radio shuttle Dexion/Constructor)

| Сенсор | Решение Dexion | Наш аналог (V1/V3) | Источник |
| --- | --- | --- | --- |
| «Bumper» | **боковые резиновые демпферы** — смягчают контакт с end-stoppers (чисто механика, НЕ датчик); столкновение предотвращается измерением, а не бампером | наши 4 GPIO-бампера — датчики удара (FALLING-IRQ) | [Dexion manual §2.4.2](https://sec-group.co.uk/wp-content/uploads/2025/01/EN-DEXION_CONSTRUCTOR-PALLET-SHUTTLE-SYSTEM-User-Manual-Issue-26.03.2014.pdf) |
| Дистанция/замедление | лазер дистанции: **снижение скорости в зоне 1500–800 мм до конца канала**; стоп по срабатыванию индуктивного концевика | ToF-замедление + концевые пороги | там же |
| Паллет-детекция | 4 позиционных лазера сверху, **попарно параллельно front/rear**; точность инвентаризации зависит от состояния паллет («missing bottom boards may result in an erroneous inventory») | наши 4 паллет-датчика в парах F1/F2, R1/R2 | там же |
| Защита людей | оптический барьер спереди/сзади; в LIFO задний отключён | (нет — F2: внешние ограждения) | там же |
| Лифтер | **магнитные переключатели** конечных позиций подъёма | DL_UP/DL_DOWN механические концевики, active-LOW | там же |
| Наличие рельса | магнитный переключатель: **лазеры включаются только когда шаттл на рельсе** | CHANNEL-датчик | там же |
| Мониторинг движения | **cogwheel на валу приводного ролика + индуктивный датчик** — контроль вращения | AS5600 (v1-actuation §6) | там же |
| E-stop | кнопки на корпусе спереди и сзади, с latch и разблокировкой поворотом + сброс ошибки через remote | (V3: force-stop по CAN — программный путь) | там же |

Дополнительные паттерны:

- 4-way shuttle (патент): датчики дистанции на корпусе в обоих направлениях X и Y **плюс датчики на подъемном механизме** для детекции паллет на полках при поднятом домкрате ([US 12724426](https://exa.ai/library/legal/patent/ys33qm48wcfzmq0dynnz1b)) — паллет-сенсоры на лифт-узле как отдельный класс размещения.
- Transfer-car (RGV): RFID + магнитное позиционирование (±3 мм), лазерное obstacle avoidance, **edge contact switches** (контактные выключатели по краям) и e-stop кнопки ([REMARKABLE RGV-10t](https://rmktransfercart.com/products/rgv-rail-guided-transfer-cart.html)).
- Фотоэлектрика для паллет в холодильных СВХ: through-beam вместо retroreflective из-за загрязнений («solutions with reflectors do not offer process reliability due to contamination»), с запасом по мощности до −40 °C ([wenglor, presence check of pallets](https://www.wenglor.com/en/Presence-Check-Using-Through-Beam-Sensors/a/125)); retroreflective area-сенсоры с широкой полосой против многоразового срабатывания на неоднородных объектах ([Pepperl+Fuchs R305](https://files.pepperl-fuchs.com/webcat/navi/productInfo/doct/tdoct9751__eng.pdf)).

### 3.2 Полярности и типы: индустриальные конвенции

- **Safety-входы — NC (normally closed)**: «The foundation of fail safe wiring uses Normally Closed (NC) contacts for safety-critical inputs. This ensures a broken wire appears as an open circuit—identical to an alarm condition» ([Industrial Monitor Direct, fail-safe circuit design](https://industrialmonitordirect.com/blogs/knowledgebase/fail-safe-circuit-design-xic-xio-wiring-methods-for-plcs)). Safety-концевики — с positive opening (принудительное размыкание при сваривании контакта) ([Omron, Technical Explanation for Safety Components (PDF)](https://edata.omron.com.au/eData/Safety/safetycompo_tg.pdf)).
- **PNP/NPN ≠ NO/NC**: «PNP tells you the current direction; NO/NC tells you which target state turns that output path on»; при приёмке записывают part number, supply range, output polarity, NO/NC, connector, max current, residual voltage, leakage — «Do not infer PNP from the housing or cable color alone» ([xszsensor, PNP sensor wiring guide](https://xszsensor.com/pnp-sensor-wiring-diagram-with-simple-step-by-step-guide/)).
- **NPN-NO (открытый коллектор к 0V) совместим с нашим active-LOW + INPUT_PULLUP**: притягивание линии к GND = «объект есть» = логический 0 (v1-sensor-io-facts §Ключевые выводы, п.3). PNP-датчик потребовал бы инверсии или подтяжки к рейке на стороне датчика (в netlist подтяжек нет).
- **Бамперы-датчики**: для контактных edge-систем фиксируются **cushion factor** (ход после сигнала) и сумма response time (edge + control + mechanical stop); тест-объекты по EN 1760 ([Rockwell Safedge manual (PDF)](https://support.rockwellautomation.com/cc/okcsFattachCustom/get/1138625_5); стандарты семейства EN 1760 / ISO 13856-2 — pressure-sensitive edges).

### 3.3 Как фиксировать при приёмке (индустриальный протокол I/O-комиссинга)

Практики PLC-комиссинга, переносимые на шаттл:

1. **Каждый дискрет проверять в обоих состояниях** (цель есть / цель нет): физическое срабатывание → LED на модуле → состояние в ПО → запись в IO-байндер ([Industrial Monitor Direct, PLC IO testing & commissioning](https://industrialmonitordirect.com/blogs/knowledgebase/plc-io-testing-and-commissioning-best-practices-for-first-time-engineers); FAT/SAT-версия: «Toggle field contact → State changes reflect in HMI/SCADA», [FAT and SAT procedures](https://industrialmonitordirect.com/blogs/knowledgebase/fat-and-sat-procedure-guidelines-for-industrial-control-systems)).
2. **Отрицательные/отказные тесты**: fail-open (обрыв), fail-short (КЗ), шум — по каждому критичному входу ([SmartLoadingHub](https://www.smartloadinghub.com/insights/warehouse-robotics/asrs-decision-matrix-fat-sat-checklist/)); попытка операции, которая должна быть заблокирована интерлоком → подтверждение блокировки ([IMD IO testing](https://industrialmonitordirect.com/blogs/knowledgebase/plc-io-testing-and-commissioning-best-practices-for-first-time-engineers)).
3. **IO-map как контракт**: имя сигнала, полярность, номинальное напряжение, update rate, failure-mode behavior, watchdog timeout, диагностический код, restart behavior; safety-состояния — явный FSM (RUN / SAFE STOP / EMERGENCY STOP / FAULT HOLD / MAINTENANCE) с тестом переходов под fault-injection ([SmartLoadingHub ASRS checklist](https://www.smartloadinghub.com/insights/warehouse-robotics/asrs-decision-matrix-fat-sat-checklist/)).
4. **Пороги и геометрия детекции**: для дистанционных сенсоров фиксировать фактические дистанции срабатывания на реальных паллетах (шаблон Dexion: 1500–800 мм зона замедления; «foils covering the pallets may cause an error» — плёнка на паллетах = источник ложных срабатываний, зафиксировать её наличие/отсутствие).
5. **Запись конфигурации per-device**: что именно стоит (part number, NPN/PNP, NO/NC, питание) — как «acceptance interfaces should include formal test points for discrete logic» ([SmartLoadingHub pallet shuttle improvement](https://www.smartloadinghub.com/insights/warehouse-robotics/pallet-shuttle-improvement-architecture-interfaces-near-term/)).

### 3.4 Перенос в T5 (#108): сверка с UNKNOWN-списком v1-actuation / v1-sensor-io-facts

| UNKNOWN | Индустриальная гипотеза (проверить на полигоне) | Как мерить/фиксировать |
| --- | --- | --- |
| Тип бамперов (п.3 actuation) | механический NO к GNDREF (FALLING-IRQ совместим) ИЛИ контактный edge-модуль | подать/убрать цель, проверить оба состояния, LED, край; зафиксировать NO/NC и наличие выделенной электроники (по Rockwell-классу — с control unit) |
| Тип паллет-датчиков (п.4) | индуктивный NPN-NO (доски/метизы) или фото/отражательный; через-beam надёжнее retro в пыли | цель = фрагмент паллеты (доска, металл); оба состояния; замер фактической дистанции; зафиксировать NPN/PNP, NO/NC, питание 5V-домен |
| Концевики лифтера | механические или магнитные (Dexion ставит магнитные) | оба состояния + скорость переключения; NO/NC |
| CHANNEL-датчик | индуктивный/фото наличие рельса (Dexion — магнитный на рельсе) | позиция в канале/вне канала, оба состояния |
| Монтаж AS5600 | вал мотора vs ось колеса; Dexion-паттерн — cogwheel на приводном ролике (мониторинг вращения, не позиция) | провернуть колесо вручную, посмотреть счётчик; сверить разрешение на оборот |
| ToF вариант B/C/D | — | серийный/маркировка модуля |
| BMS модель/химия | — | шильдик, ответ DeviceInfo 0x05 |
| E22 вариант | — | маркировка |
| Масса/геометрия | — | взвешивание/обмер; вход в D_brake-модель |

Отдельный пункт: **фиксировать, что safety-дискреты при обрыве уходят в безопасную сторону**. Наши паллет/концевики — NO-семантика (active-LOW при замыкании на GND): обрыв = «нет объекта» / «не нажат», что для паллет-детекции не fail-safe (паллета «пропадает») — это соответствует V1-проектированию (паллет-сенсор не safety, HZ-11), но должно быть записано в T5-протоколе явно, чтобы не путать с бамперным контуром.

## 4. HIL/IOT-стенды паллетных шаттлов и transfer-car

### 4.1 Протокольные эмуляторы у интеграторов (что реально делают)

| Компания | Стенд | Суть | Источник |
| --- | --- | --- | --- |
| **Savant Automation** | iQ-CAN VPath Emulator L3, System Emulation Mode | виртуальный AGV-флот поверх CAD-макета заказчика; тестируется traffic control, routing, station cycles; **«The program literally does not know the simulated vehicles are not real»** — симулируемые AGV «репортят» статус/локацию с таймстампами; конфиг, провалидированный в эмуляции, грузится в реальный site-контроллер | [agvsystems.com VPath Emulator L3](https://agvsystems.com/?page_id=6078%2F) |
| **Emulogix** | AS/RS System Emulator | эмулятор нескольких S/R-машин одновременно, «**orchestrates the parts of the AS/RS machine as if it was onsite, responding to PLC outputs, and returning meaningful PLC inputs**»; time distortion — «test all possible AS/RS behaviors within a few minutes... on site would take weeks» | [emulogix.com ASRS](http://www.emulogix.com/asrs_system.html), [system view](http://www.emulogix.com/asrs_overview.html) |
| **SSI Schäfer** | WAMAS Emulation Center | эмуляция PLC-управления складом + воспроизведение поведения системы; тесты обновлений до live, end-to-end от goods-in до goods-out, «Black Friday scenario», обучение персонала | [ssi-schaefer.com WAMAS EC](https://www.ssi-schaefer.com/en-de/software/wamas-software-solutions/wamas-emulation-center) |
| **Daifuku** | Sym3 (simulation + emulation) | «emulation which is the **factory testing of control systems**... test both our PLC and SCADA control systems, prior to site commissioning»; FAT platform с заказчиком, линк к внешнему host | [daifuku.com Sym3](https://www.daifuku.com/solution/intralogistics/products/wms/sym3/) |
| **Dematic** | Warehouse Emulation | изолированная цифровая среда, прямой коннект к host; валидация интеграций, peak volumes, what-if исключения | [dematic.com warehouse emulation](https://www.dematic.com/en-us/insights/articles/warehouse-simulation-software/) |
| **Swisslog** | SynQ subsystem emulation + Digital Twin | controlled testing without physical hardware, движение TU end-to-end, валидация deployment | [swisslog.com SynQ](https://www.swisslog.com/en-us/products-systems-solutions/software-inventory-management/synq-warehouse-management-system-wms-mfcs) |
| **ek robotics (E&K Automation)** | material flow simulation | 3D-симуляция транспортной системы для определения оптимального числа машин; компания исторически ведёт «layout development and commissioning of driverless transport systems» | [ek-robotics.com 60 years AGV](https://ek-robotics.com/en/about-us/60-years-agv/) |

Общий паттерн: **эмулятор — это протокольный узел, который отвечает на выходы реального контроллера и возвращает осмысленные входы; физика не моделируется, моделируется интерфейс**. «Emulation is a functional test environment... It executes real control logic and evaluates real equipment interactions inside the model» ([Steele Solutions, emulation-based commissioning](https://www.steelesolutions.com/automation-emulation-based-commissioning/)). Это дословно наша архитектура T3 (#109: эмулятор = CAN-node, SET_DUTY на controller_id) и решение карты #103 («приводной реализм = протокольный эмулятор, физика — полигон»).

Академический вариант того же: эмуляция AGV-маршрутов в Tecnomatix Plant Simulation в смешанной real/virtual среде ([MDPI Appl. Sci. 12(7):3397](https://www.mdpi.com/2076-3417/12/7/3397)); патент на эмулятор AGV-систем (сенсорные данные виртуальной машины → система → motion control обратно) ([US 20230289494](https://www.patents-review.com/a/20230289494-method-apparatus-emulating-automated-guided-vehicle-agv.html)).

### 4.2 BMS- и приводные стенды

- **dSPACE SCALEXIO Battery HIL**: эмуляция ячеек и CSC на signal level (CAN, SPI, isoSPI, UART, I²C интерфейсы к DUT) и на HV level; «integrated electrical failure simulation of open wires, short circuits, and high-frequency ripple voltages»; ASM-модели батарей real-time; используются в т.ч. для release/acceptance-тестов ([dSPACE BMS Testing Solution](https://www.dspace.com/en/inc/home/applicationfields/ind-appl/automotive-industry/emobility/battery-management-systems/bms-testing-solution.cfm)). Прямой паттерн для нашего RS-485 BMS-симулятора (DdA5-стимулятор, #110 BOM) и I2C-stuck инъекций.
- **Bloomy FLEX BMS Validation System**: до 48 эмулируемых ячеек, термисторы, токи до 600 A, симуляция всех BMS I/O и коммуникаций; regression-скрипты; используется safety-labs для сертификации BMS ([bloomy.com](https://www.bloomy.com/products/battery-management-system-testing/flex-bms-validation-system)).
- **EMEC development/EoL-стенд** (хаб-моторы e-bike): measuring shafts (torque/speed), температура, автозапуск тест-цикла после установки, **«CAN interface for listen to CAN and send to CAN»**, автоматический протокол по DIN EN 15194; EoL-расширение с автофиксацией и грейн/фейл-подачей моторов ([EMEC DTB PDF](https://www.emec-prototyping.com/fileadmin/user_upload/Downloads/2025_EMEC_DTB_EoL_ENG.pdf)).
- **P Robots «Smoov» 4-way shuttle** (реальный 4-way шаттл, Beckhoff CX IPC): контрольная архитектура на промышленном IPC с PLC — подтверждает класс (наш STM32-класс — ниже по сложности, но паттерны те же) ([Unimore thesis PDF](https://unitesi.unimore.it/bitstream/20.500.14251/4824/1/Morselli.Alessandro.pdf)).

### 4.3 Практики воспроизводимости HIL (для T1 матрицы и verification-runner #58)

- **«Bench acts like a build system, not an experiment»**: versioned bench profile (I/O map, solver step, scaling), firmware/calibration из tagged builds, deterministic stimulus на таймстампах, numeric pass/fail, сигналы+metadata capture, rerun только для bench-дефектов с записанной причиной ([OPAL-RT, HIL regression pipeline](https://www.opal-rt.com/blog/building-a-regression-testing-pipeline-for-hil-in-electrification-teams/)). У нас уже отражено: workload-матрица + N≥30 + observed maxima (#52 §6.3).
- **«Ten stable tests will beat fifty fragile ones. Safety and protection checks come first»** — приоритезация стабильных safety-тестов над длинными флейковыми suite'ами (там же) = наши 6 обязательных acceptance release-gate.
- **Паттерн «sim-grid + hardware spot-validation»** для stopping distance: параметризованная сетка скорость×нагрузка×препятствие в симе, выборочные аппаратные прогоны для подтверждения ([roboticks, ISO 3691-4 mapping](https://docs.roboticks.io/standards/iso-3691-4)) — переносимо в T1: эмуляторные сценарии по сетке + полевые spot-замеры D_brake.
- **MCAP/evidence-захват каждого события детекции** (TF, команды, stop-confirmation) (там же) — наш аналог: event-метки с monotonicTick + CAN-анализатор (#52 §6.3 reference instant).

### 4.4 Импликации для T3 (эмулятор VESC-subset, #109)

1. **Эмулятор = «responding to outputs, returning meaningful inputs»** (Emulogix-паттерн): принимать SET_DUTY/SET_CURRENT на controller_id 100/101, эмиттить CAN_PACKET_STATUS (9) с реалистичными units (ток 0.1 A, duty 0.001, ERPM) на настраиваемом rate (default-практика ~20 Hz, [OpenRobot](https://dongilc.gitbook.io/openrobot-inc/tutorials/control-with-can)).
2. **Клампинг вне-диапазонных команд** — эмулировать (comm_can.md), иначе контракт проверки нечестен.
3. **Инъекционные режимы**: timeout-эмуляция (моделирование 0.5 s молчания → наблюдаемая реакция контроллера V3), flood, bus-off/error-passive, «зависший» статусный поток — карта fault-injection #52 §6.2.
4. **Time distortion не переносить**: на Emulogix она работает потому, что эмулируется whole-plant для PLC-логики; для нас тайминги T_fs/T_eso — предмет измерения, искажение часов разрушит oracle O4.
5. **Не эмулировать физику привода** (момент, замедление) — это решение карты #103 и консенсус индустрии (раздел 1.3: brake/нагрузка всегда полевые).

## 5. Сводка рекомендаций (deliverable)

### 5.1 Разделы сайт-чек-листа T5 (тикеты #108, #111)

1. **Привод: fail-safe-профиль per-device** — measured drive-timeout, timeout brake current, duty0-реакция (замер), направление, статусные кадры (ID/rate/units), токовые лимиты. (Шаблоны: VESC comm_can.md; MiR/arculus/ZAPI.)
2. **Brake-методика** — полная нагрузка + полная скорость, оба направления, с нагрузкой и без, замер дистанции, порог записать числом (у MiR — 20 см; наш порог = D_accept 1.0 м по C1, #48). Периодический ре-тест в план ТО (INV-BRAKE-VALIDITY). (Шаблоны: MiR §3.5, arculus, mayr.)
3. **Сенсорика: протокол обоих состояний** — для каждого из 10 дискретов (4 бампера, 4 паллет, 2 лифтер, канал): тип (NPN/PNP, NO/NC), питание, активный уровень, оба состояния, LED, реакция прошивки, серийник; для бамперов — cushion factor/response time, если edge-модуль. Отдельно: тест обрыва для каждого критичного входа с записью фактического поведения (не все NO-цепи fail-safe — фиксировать, какие). (Шаблоны: IMD, SmartLoadingHub, Rockwell, xszsensor.)
4. **Геометрия/пороги детекции** — фактические дистанции срабатывания паллет-датчиков на реальных паллетах (и на завёрнутых в плёнку — Dexion-предостережение), пороги замедления ToF, состояние стеллажа/end-stoppers, ширина/состояние канала. (Шаблон: Dexion §5.3/5.8, §2.4.2.)
5. **Конфигурация per-device** — profile, калибровки AS5600 (монтаж подтверждён), параметры BMS/E22/ToF-вариантов. (Шаблон: CONTREX-стайл startup-калибровка.)

### 5.2 Разделы сценарной матрицы T1/T4 (#107, #113)

- **T3-стенд**: контракт команд и клампинг; keep-alive/timeout-семантика; force-stop арбитраж (min-ID эмуляция); flood/bus-off; статусный поток rate; — источник: comm_can.md, Emulogix-паттерн, OPAL-RT.
- **T4-полигон**: фактический fail-safe привода (timeout/brake/duty0) — вход в решение F4 #113; D_brake(v, load) MiR-методикой; направление/полярности; ток лифтера при реальном грузе (порог 50 A верифицировать физическим зажимом — U-пункт «зажим 250»); U10-стоп-паттерн на реальном приводе.
- **Приёмка v1.0.0** (#111): per-device commissioning Q7.1 A (fail-safe-тест перед Ready) = индустриальный стандарт-практика (ZAPI/MiR/ISO 3691-4 commissioning); EN 528-стайл: интегрированный commissioning после individual elements; FEM 9.222-стайл: function → performance → availability как три разных класса evidence с разным oracle; running-in после приёмки.

### 5.3 Anti-паттерны, которых избегает индустрия (проверка нашей карты)

- Эмулировать торможение/физику на стенде ради приёмочного зачёта — не делает никто (раздел 1.3).
- «Jump straight to full speed because the FAT passed» (Path2) — соответствует нашему поэтапному переносу и staged speed.
- Слепое доверие документации: «the stronger audits also check whether each control is validated in the real site, not just declared in technical documents» (sectorreviewhq) — соответствует правилу «V1 — свидетельства, не норматив» и измерительной парадигме #48.
- Ручной режим без safety: Dexion открыто пишет «In the manual mode of operation the safety devices of the pallet shuttle are not functional» — наша V3-модель строже (motion-инварианты и в manual, INV-SENSING-FRESH), это осознанное усиление, не отступление.

## 6. Ограничения и confidence

- Стандартные тексты (ISO 13849-2, ISO 3691-4, EN 528, VDI 2710-5, ISO 12100) прочитаны в объёме бесплатных previews/официальных описаний: структура и требования переданы по смыслу, точные численные значения (например, множители test load EN 528 §5.4) не воспроизводятся — при необходимости закупки полного текста это отдельное HITL-действие. Confidence: средний-высокий на структуру, без цифр.
- SmartLoadingHub / sectorreviewhq / industrialmonitordirect — отраслевые аналитические блоги, не первичные нормативы; использованы как консолидация практик FAT/SAT/IO. Confidence: средний; каждый такой вывод продублирован первичным шаблоном (ZAPI/MiR/мануал Dexion/стандарт-структура), где возможно.
- VESC-факты: официальная документация репозитория — высокий confidence; forum-высказывания автора firmware (node/134, node/938) — первично-авторские, но не норматив — помечено; конкретное поведение FSESC75100 (Flipsky-клон VESC) до полигонной проверки остаётся гипотезой с высокой априорной вероятностью — закрытие в #104/#108.
- Патентные источники (US12724426, US20230289494) использованы только как свидетельство существования паттерна, не как руководство.

## 7. Источники (полный список)

**Производители шаттлов / AGV / transfer-car:**

1. Dexion/Constructor Pallet Shuttle User Manual (26.03.2014): <https://sec-group.co.uk/wp-content/uploads/2025/01/EN-DEXION_CONSTRUCTOR-PALLET-SHUTTLE-SYSTEM-User-Manual-Issue-26.03.2014.pdf>
2. CONTREX M-Shuttle Installation/Start-Up Guide: <https://www.contrexinc.com/PDF/UserGuides/M-ShuttleInstallGuide.pdf>
3. MiR500/MiR1000 Commissioning Guide: <https://all-guidesbox.com/manual/1714203/mir-mir500-commissioning-manual-13.html>
4. arculus, «Commissioning AMRs: This is How We Do It»: <https://www.arculus.de/commissioning-amrs>
5. Savant Automation, VPath Emulator L3: <https://agvsystems.com/?page_id=6078%2F>
6. Emulogix AS/RS System Emulator: <http://www.emulogix.com/asrs_system.html>, <http://www.emulogix.com/asrs_overview.html>
7. SSI Schäfer WAMAS Emulation Center: <https://www.ssi-schaefer.com/en-de/software/wamas-software-solutions/wamas-emulation-center>
8. Daifuku Sym3: <https://www.daifuku.com/solution/intralogistics/products/wms/sym3/>
9. Dematic Warehouse Emulation: <https://www.dematic.com/en-us/insights/articles/warehouse-simulation-software/>
10. Swisslog SynQ (subsystem emulation): <https://www.swisslog.com/en-us/products-systems-solutions/software-inventory-management/synq-warehouse-management-system-wms-mfcs>
11. ek robotics (E&K Automation), 60 years AGV: <https://ek-robotics.com/en/about-us/60-years-agv/>
12. REMARKABLE RGV Rail Guided Transfer Cart: <https://rmktransfercart.com/products/rgv-rail-guided-transfer-cart.html>
13. ZAPI COMBI AC0 manual: <https://dc66.ru/upload/iblock/6c8/cdx95fc18wlcn9n59xysqdqp593uugmn.pdf>
14. mayr, «More Than Just Braking» (AGV safety brakes, t1): <https://www.mayr.com/en/blog/more-than-just-braking~54418>; ROBA brake-checker: <https://www.mayr.com/en/products/safety-brakes/supply-and-monitoring-components/roba-brake-checker~2134>
15. INVT Goodrive20-09 VFD manual (hoisting, torque verification): <https://www.invt.com/uploads/file1/20250902/GD20-09%20VFD%20Manual_V1.1.pdf>
16. Performance Validation, кейс Dematic ASRS (engineering shakedown): <https://perfval.com/case-study/dematic-automated-storage-and-retrieval-system-validation/>; ASRS CQV: <https://perfval.com/solution/automated-storage-and-retrieval-system-asrs/>
17. Flipsky 75100 (привод шаттла, VESC-based): <https://flipsky.net/products/75100-75v-100a>

**VESC (наш класс привода):**

1. vedderb/bldc, `documentation/comm_can.md` (timeout, brake current, таблица команд, клампинг, статус-кадры): <https://github.com/vedderb/bldc/blob/master/documentation/comm_can.md>
2. VESC Project, node/134 «Duty cycle mode with neutral» (Benjamin Vedder: FOC duty < back-EMF = braking): <https://vesc-project.com/node/134>
3. VESC Project, node/938 «Motor Wizard FOC» (wheels off ground, старт 15–20 A, Inverted): <https://vesc-project.com/node/938>
4. VESC Project, node/183 (Timeout Brake, Acceleration Temperature Decrease): <https://vesc-project.com/node/183>
5. VESC6 CAN Formats (PDF): <https://vesc-project.com/sites/default/files/imce/u15301/VESC6_CAN_CommandsTelemetry.pdf>
6. OpenRobot, «Control with CAN» (формат EID, units статус-кадров): <https://dongilc.gitbook.io/openrobot-inc/tutorials/control-with-can>

**Стандарты и нормативные шаблоны:**

1. ISO 13849-2:2012 (preview): <https://cdn.standards.iteh.ai/samples/53640/e240593c209440729727cd6ec30d7a94/ISO-13849-2-2012.pdf>
2. ISO 3691-4:2023 (card): <https://www.iso.org/standard/83545.html>; TÜV Rheinland whitepaper: <https://www.tuv.com/content-media-files/master-content/services/industrial-services/pdf/tuv-rheinland-automatic-guided-vehicles-whitepaper-en_neu.pdf>
3. EN 528:2021 (preview): <https://cdn.standards.iteh.ai/samples/68054/a458e1b2fdc84094b2612d96abba0ce5/SIST-EN-528-2021.pdf>
4. VDI 2710 Blatt 5 (TOC preview): <https://www.vdi.de/fileadmin/pages/vdi_de/redakteure/richtlinien/inhaltsverzeichnisse/2074815.pdf>
5. FEM 9.222 (function/performance/availability tests, running-in): <https://www.fem-eur.com/wp-content/uploads/2016/03/FEM-9_222_Englisch.pdf>
6. ISO 12100:2010 (card): <https://www.iso.org/standard/51528.html>

**Сенсорика и I/O-комиссинг:**

1. Rockwell Safedge (cushion factor, response time, EN 1760 test objects): <https://support.rockwellautomation.com/cc/okcsFattachCustom/get/1138625_5>
2. Omron, Technical Explanation for Safety Components (NC fail-safe, positive opening): <https://edata.omron.com.au/eData/Safety/safetycompo_tg.pdf>
3. Industrial Monitor Direct, fail-safe XIC/XIO wiring: <https://industrialmonitordirect.com/blogs/knowledgebase/fail-safe-circuit-design-xic-xio-wiring-methods-for-plcs>
4. Industrial Monitor Direct, PLC IO testing & commissioning: <https://industrialmonitordirect.com/blogs/knowledgebase/plc-io-testing-and-commissioning-best-practices-for-first-time-engineers>
5. Industrial Monitor Direct, FAT/SAT procedures: <https://industrialmonitordirect.com/blogs/knowledgebase/fat-and-sat-procedure-guidelines-for-industrial-control-systems>
6. xszsensor, PNP sensor wiring (PNP≠NO/NC, протокол записи): <https://xszsensor.com/pnp-sensor-wiring-diagram-with-simple-step-by-step-guide/>
7. wenglor, presence check of pallets (through-beam vs retro): <https://www.wenglor.com/en/Presence-Check-Using-Through-Beam-Sensors/a/125>
8. Pepperl+Fuchs R305 area sensor: <https://files.pepperl-fuchs.com/webcat/navi/productInfo/doct/tdoct9751__eng.pdf>

**HIL/стенды/эмуляция:**

1. dSPACE BMS Testing Solution (SCALEXIO Battery HIL): <https://www.dspace.com/en/inc/home/applicationfields/ind-appl/automotive-industry/emobility/battery-management-systems/bms-testing-solution.cfm>
2. Bloomy FLEX BMS Validation System: <https://www.bloomy.com/products/battery-management-system-testing/flex-bms-validation-system>
3. EMEC Development Test Bench MDU/HUB (EoL, CAN listen/send): <https://www.emec-prototyping.com/fileadmin/user_upload/Downloads/2025_EMEC_DTB_EoL_ENG.pdf>
4. OPAL-RT, HIL regression pipeline: <https://www.opal-rt.com/blog/building-a-regression-testing-pipeline-for-hil-in-electrification-teams/>
5. Steele Solutions, emulation-based commissioning: <https://www.steelesolutions.com/automation-emulation-based-commissioning/>
6. Festo, digital twin & virtual commissioning (FMI/FMU): <https://www.festo.com/us/en/e/solutions/digital-transformation/digital-twin-and-virtual-commissioning-id_1643059>
7. Roboticks, ISO 3691-4 / ANSI R15.08 requirement mapping (sim-grid + HW spot-validation): <https://docs.roboticks.io/standards/iso-3691-4>, <https://docs.roboticks.io/standards/ansi-r15-08>
8. Schyga et al., T&E 4Log localization framework: <https://arxiv.org/pdf/2107.10597>
9. ASTM F3499-21 (A-UGV docking performance): <https://www.en-standard.eu/astm-f3499-21-standard-test-method-for-confirming-the-docking-performance-of-a-ugvs/>
10. Path2 Robotics, robot cell commissioning (staged speed): <https://path2.io/resources/robot-cell-commissioning-guide>
11. merixa, commissioning services (loop checks до движения): <https://merixa.com/services/commissioning>
12. MDPI, AGV route verification via emulation: <https://www.mdpi.com/2076-3417/12/7/3397>
13. US Patent 12724426 (four-way shuttle obstacle detection): <https://exa.ai/library/legal/patent/ys33qm48wcfzmq0dynnz1b>; US 20230289494 (AGV system emulation): <https://www.patents-review.com/a/20230289494-method-apparatus-emulating-automated-guided-vehicle-agv.html>
14. Unimore thesis, P Robots Smoov 4-way shuttle telemetry: <https://unitesi.unimore.it/bitstream/20.500.14251/4824/1/Morselli.Alessandro.pdf>

**Аналитика FAT/SAT (консолидация практик):**

1. SmartLoadingHub, ASRS Decision Matrix and FAT/SAT Checklist: <https://www.smartloadinghub.com/insights/warehouse-robotics/asrs-decision-matrix-fat-sat-checklist/>
2. SmartLoadingHub, pallet shuttle improvement (I/O map как контракт): <https://www.smartloadinghub.com/insights/warehouse-robotics/pallet-shuttle-improvement-architecture-interfaces-near-term/>
3. SmartLoadingHub, pallet shuttle failures (acceptance criteria, 100 cycles): <https://www.smartloadinghub.com/insights/warehouse-robotics/when-pallet-shuttle-failures-disrupt-flow/>
4. SmartLoadingHub, AMR/AGV rollout planning: <https://www.smartloadinghub.com/insights/agv-amr/practical-planning-amr-agv-robot-rollout/>
5. sectorreviewhq, ISO 3691 checklist for AGV site acceptance audits: <https://www.sectorreviewhq.com/news/Forklifts_Handling_Vehicles/AGV_Tow_Tractors/ISO_3691_Checklist_for_AGV_Safety_Audits_What_to_Review_Before_Site_Acceptance.html>
6. GoASRS, implementation timeline: <https://goasrs.com/asrs-implementation-timeline/>
