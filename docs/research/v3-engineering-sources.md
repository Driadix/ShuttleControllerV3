# Нормативные источники для инженерной программы firmware V3

Дата исследования: **2026-07-31**. Тикет: [#7](https://github.com/Driadix/ShuttleControllerV3/issues/7).

## 1. Вопрос и границы вывода

Цель исследования: определить первичные стандарты и официальные инженерные руководства, из которых разумно вывести требования и проверяемые критерии промышленной embedded-прошивки V3 на фиксированной плате STM32. Обязательные ограничения проекта: Arduino Core for STM32 и PlatformIO, детерминированное неблокирующее исполнение, разработка от спецификации, модульность и тестируемость, полный производственный жизненный цикл, безопасное OTA-обновление с откатом, инженерный анализ опасностей/безопасных состояний и пирамида верификации с HIL.

Этот документ **не заявляет соответствие, сертификацию или квалификацию** по какому-либо стандарту. Полные тексты большинства ISO/IEC/IEEE и MISRA платные; официальные страницы раскрывают область применения, но не все нормативные требования. Поэтому из платных документов ниже заимствуются только подтверждаемые открытым официальным описанием идеи. Перед заявлением соответствия потребуются легально приобретенные применимые редакции, полный gap analysis, доказательства выполнения всех выбранных положений и, где требуется, независимая оценка.

Классы решения:

- **A, принять кандидатом в проектный gate**: практика полезна при данных ограничениях и может быть превращена в локальный проверяемый критерий без заявления соответствия источнику.
- **B, только справочно**: источник формирует словарь, структуру или варианты решения; конкретный gate нужно адаптировать после проектного решения.
- **C, неприменимо или избыточно сейчас**: формальная программа оправдана только при регуляторной/контрактной потребности либо иной отрасли.

## 2. Рекомендуемый минимальный набор

| Область | Основные источники | Решение |
|---|---|---|
| Жизненный цикл | ISO/IEC/IEEE 12207:2026 | B как карта процессов; не принимать весь стандарт |
| Требования | ISO/IEC/IEEE 29148:2018; ISO/IEC 25010:2023 | A: качество, идентификаторы, трассировка и измеримые критерии; B: словарь качеств |
| Архитектура | ISO/IEC/IEEE 42010:2022 | A: stakeholders/concerns/viewpoints, решения и связи требований с архитектурой |
| C++ | ISO C++17; MISRA C++:2023; SEI CERT C++ | A: локальный профиль C++17 и журнал отклонений; B: MISRA/CERT как источники правил |
| Статический анализ | документация GCC/Clang, SEI CERT | A: предупреждения компилятора и не менее одного анализатора в CI, baseline и zero-new-findings |
| Опасности и safe state | IEC 60812:2018; IEC 61025:2006; IEC 61508:2010 | A: адаптированные FMEA/FTA и проверяемые safe states; B/C: IEC 61508 без SIL/сертификации |
| Secure update | NIST SP 800-193; RFC 9019/9124; ST SBSFU/MCUboot | A: подпись, совместимость, anti-rollback, атомарность, подтверждение boot и recovery |
| Наблюдаемость | ISO 17359:2018; NIST SP 800-193; RFC 9019 | A: диагностический контракт и post-update status; B: ISO 17359 как программа condition monitoring |
| Верификация | IEEE 1012-2024; ISO/IEC/IEEE 29119-2:2021; PlatformIO Unit Testing | A: трассируемая пирамида host/target/integration/HIL; B: полные процессные модели |
| Конфигурация/релиз | IEEE 828-2012; NIST SP 800-218 v1.1; PlatformIO package docs | A: version pinning, идентифицируемая сборка, provenance, подписанный release и rollback package |

## 3. A: практики-кандидаты в проектные gates

Ниже перечислены именно **локальные кандидаты**, а не воспроизведенные нормативные требования.

### A1. Требования и трассировка

Основание: [ISO/IEC/IEEE 29148:2018](https://www.iso.org/standard/72089.html) охватывает процессы requirements engineering, создаваемые information items и их содержание; [ISO/IEC 25010:2023](https://www.iso.org/standard/78176.html) задает модель характеристик качества для спецификации, измерения и оценки продукта.

Кандидаты в gates:

- Каждое нормативное требование имеет стабильный ID, источник/обоснование, приоритет, условия применимости и проверяемый критерий приемки.
- Требования отделяют функцию от ограничений качества: timing, capacity/resources, reliability/recovery, security, maintainability/testability и observability.
- Для каждого требования прослеживаются архитектурное решение, реализация и один или несколько способов verification/validation; orphan requirements и непокрытые проверки блокируют релиз либо имеют утвержденное обоснование.
- Формулировки задают наблюдаемые условия и границы: режим, входы, deadline/latency, допустимый результат и поведение при отказе. Слова «быстро», «надежно», «безопасно» без меры не проходят review.
- Нормативные документы проекта ведутся на русском; английские термины стандартов сохраняются рядом при первом употреблении, чтобы не потерять смысл и трассируемость.

### A2. Архитектурное описание

Основание: [ISO/IEC/IEEE 42010:2022](https://www.iso.org/standard/74393.html) определяет структуру architecture description, concepts/relationships, viewpoints и model kinds, но не предписывает метод проектирования.

Кандидаты в gates:

- Архитектурный документ называет систему интереса, заинтересованные стороны, их concerns, контекст и внешние интерфейсы фиксированной PCB.
- Отдельные представления покрывают как минимум: runtime/state and scheduling; software modules/dependencies; hardware/resource allocation; data/configuration; update/trust boundaries; verification seams.
- Архитектурно значимые решения фиксируют контекст, варианты, выбранный вариант, последствия и связанные требования/риски.
- Зависимости проходят только через заявленные интерфейсы; hardware/Arduino API изолируются адаптерами так, чтобы доменная логика тестировалась на host.
- Для каждого периодического/событийного пути определены budget, максимальное время шага, частота вызова, политика очереди/переполнения и реакция на deadline miss. Неблокирующий характер подтверждается review и timing-тестом, а не только стилевым правилом.

### A3. Профиль C++ и статический анализ

Основание: официальный [MISRA C++:2023](https://misra.org.uk/product/misra-cpp2023/) предназначен для C++17 в critical systems. Открытый [SEI CERT C++ Coding Standard](https://wiki.sei.cmu.edu/confluence/display/cplusplus) ориентирован на safe/reliable/secure C++; его раздел [Tool Selection and Validation](https://wiki.sei.cmu.edu/confluence/display/cplusplus/Tool+Selection+and+Validation) прямо отмечает ограничения статического анализа и необходимость оценки инструмента. Фактический PlatformIO build script Arduino Core STM32 использует `gnu++17`, `-fno-rtti` и `-fno-exceptions`: [first-party source](https://github.com/stm32duino/Arduino_Core_STM32/blob/main/tools/platformio/platformio-build.py).

Кандидаты в gates:

- Зафиксировать локальный профиль языка: C++17/конкретный dialect и toolchain, разрешенные/запрещенные возможности, правила ownership/lifetime, integer conversions, concurrency/interrupt sharing, error handling, allocation after startup и ISR.
- Не называть профиль «MISRA compliant». После приобретения MISRA можно сопоставить локальные правила с применимым подмножеством; до этого MISRA является направлением и источником тем, а не чек-листом.
- CI собирает production-конфигурацию с максимальным практически применимым набором предупреждений; предупреждения являются ошибками, а подавления точечны и обоснованы.
- CI запускает минимум один независимый static analyzer. Конфигурация, версия и baseline хранятся как configuration items; новые high-confidence findings запрещены, существующие имеют владельца/решение.
- Каждое исключение из локального coding profile содержит правило, место, инженерное обоснование, оценку риска, компенсирующую проверку и approver. Автоматический анализ не заменяет review undecidable properties.
- Проверять production build flags и ABI фактически: Arduino Core/PlatformIO являются обязательной платформой, но их defaults не считаются доказательством требуемого языка, диагностики или безопасности.

### A4. Анализ опасностей, отказов и safe state

Основание: официальная страница [IEC 60812:2018](https://webstore.iec.ch/en/publication/26359) описывает планирование, выполнение, документирование и сопровождение FMEA/FMECA для hardware, software, процессов и интерфейсов. [IEC 61025:2006](https://webstore.iec.ch/en/publication/4311) дает guidance по fault tree analysis. Это платные документы; ниже предлагается адаптированный инженерный процесс.

Кандидаты в gates:

- Вести hazard log и FMEA по функциям, состояниям, I/O, питанию, памяти/config, связи, обновлению и human interaction: failure mode, cause, local/system effect, severity, detectability, prevention/mitigation, safe state, verification и residual risk.
- Применять FTA точечно к нескольким неприемлемым top events, когда одной FMEA недостаточно показать комбинации причин.
- Для каждой опасной функции определить safe state или обоснованное degraded state, путь входа, максимальное время достижения, условия выхода и поведение при reset/brownout/watchdog/communication loss/sensor invalidity.
- Анализ обновляется при изменении требований, PCB assumptions, архитектуры, boot/update path или обнаружении field failure; открытый неприемлемый риск блокирует релиз.
- Safe-state claims проверяются fault-injection и HIL-сценариями. Сам факт наличия watchdog не считается mitigation без доказательства timeout, reset cause handling и результата после reset loop.

### A5. Secure OTA, rollback и recovery

Основание: [NIST SP 800-193, final, May 2018](https://csrc.nist.gov/pubs/sp/800/193/final) формулирует три принципа firmware resiliency: protection, detection, recovery; полный текст требует authenticated update, защищенный trust anchor и восстановимость при прерывании питания. [RFC 9019, April 2021](https://www.rfc-editor.org/rfc/rfc9019.html) задает архитектуру IoT firmware update, а [RFC 9124, January 2022](https://www.rfc-editor.org/rfc/rfc9124.html) — информационную модель manifest; оба имеют категорию **Informational**, не Internet Standard. Официальное руководство ST [SBSFU by MCUboot](https://wiki.st.com/stm32mcu/wiki/Security:SBSFU_by_MCUboot) описывает проверку authenticity/integrity и стратегии overwrite/swap для актуальных STM32; применимость зависит от точной модели MCU и flash layout.

Кандидаты в gates:

- До записи/активации проверяются авторизация подписанта, подпись manifest, digest payload, product/board/MCU compatibility и monotonic security/version policy. Transport security не заменяет end-to-end подпись.
- Manifest/release metadata однозначно связывают firmware с hardware revision, bootloader/update schema, размером, digest, версией и допустимым predecessor.
- Trust anchor и код проверки защищены доступными аппаратными механизмами STM32; процесс key generation, custody, rotation/revocation и emergency signing документирован и испытан.
- Установка атомарна относительно reset и power loss. HIL fault injection прерывает загрузку/erase/write/swap/first boot во всех критических точках и доказывает возврат к исполнимому аутентичному состоянию.
- Разделять **anti-rollback security policy** (запрет неавторизованной установки уязвимой старой версии) и **functional rollback** (возврат к последней подтвержденной рабочей версии). Откат разрешается только политикой и не обходит подпись/совместимость.
- Новая версия проходит bounded trial boot и self-test, затем явно подтверждается. Reset loop, timeout или failed health check вызывают контролируемый rollback/recovery и оставляют диагностическую причину.
- Для каждой release-кандидатуры HIL проверяет valid update, wrong key/signature, corrupt/truncated image, wrong hardware/class, downgrade, power loss, full storage, interrupted confirmation и unavailable network.
- До выбора bootloader выполнить feasibility gate на реальной PCB: MCU security features, flash/RAM, slots/scratch, wear, update transport и возможность recovery. ST SBSFU/MCUboot — предпочтительный first-party reference, но не автоматически совместим с Arduino Core/PlatformIO или конкретной платой.

### A6. Наблюдаемость и диагностика

Основание: secure update architecture RFC 9019 включает status tracking и отчет об успешной загрузке; NIST SP 800-193 требует detect/recover. [ISO 17359:2018](https://www.iso.org/standard/71194.html) относится к общей программе condition monitoring машин, поэтому используется лишь как справочная рамка, а не firmware telemetry standard.

Кандидаты в gates:

- Определить диагностический контракт: stable event/fault IDs, severity, monotonic timestamp или явно обозначенное качество времени, boot/reset cause, firmware/build/hardware identity, operating state и bounded context fields.
- Critical faults, safe-state transitions, watchdog/reset loops, update stages/outcomes и rollback reason сохраняются через reset в пределах ресурса памяти и flash endurance.
- Логирование имеет bounded time/memory, не блокирует control loop и не раскрывает ключи/секреты/чувствительные payloads. Политики overflow, rate limit и retention проверяются под нагрузкой.
- Health/status различают alive, ready, degraded и safe state; post-update success публикуется только после acceptance/self-test, а не сразу после старта.
- Для каждого диагностического сигнала указаны потребитель, решение оператора/системы и test oracle. Телеметрия «на всякий случай» не является gate.

### A7. Верификация и HIL

Основание: [IEEE 1012-2024](https://standards.ieee.org/ieee/1012/7324/) охватывает V&V system/software/hardware, включая firmware и interfaces, посредством analysis, review, inspection, assessment и testing. [ISO/IEC/IEEE 29119-2:2021](https://www.iso.org/standard/79428.html) задает generic test processes для любых lifecycle models. Официальный [PlatformIO Unit Testing](https://docs.platformio.org/en/stable/advanced/unit-testing/introduction.html) поддерживает host, embedded target и remote/CI testing.

Кандидаты в gates:

- Verification strategy сопоставляет риски и требования уровням: static/review, host unit, component, target unit, integration, system/HIL, endurance/performance и release acceptance.
- Каждый тест имеет requirement/hazard link, известную конфигурацию, входы/предусловия, автоматический oracle, ожидаемые timing bounds, результат и сохраняемый evidence artifact.
- Pure logic выполняется на host; hardware abstraction contract — на target; реальные периферия, electrical timing, resets, power faults, communication faults и update recovery — на HIL фиксированной PCB.
- CI на каждый change выполняет formatting/profile/static analysis, host tests и production build; target/HIL suite запускается по определенной политике, а полный release suite обязателен перед релизом.
- HIL bench является configuration item: board revision/serial, fixture, instruments, calibration where relevant, wiring, firmware/tool versions и environment записываются с результатом.
- Flaky test не перезапускается до green молча: он quarantine только с issue/owner/risk; непроверенный high-risk requirement блокирует release.
- Coverage применяется как индикатор пробелов, не как доказательство корректности. Exit criteria включают requirement/hazard coverage, отсутствие необъясненных failures и выполненные timing/resource budgets.

### A8. Configuration и release management

Основание: официальная страница [IEEE 828-2012](https://standards.ieee.org/ieee/828/10549/) охватывает identification/acquisition of configuration items, change control, status accounting, builds и release engineering. [NIST SP 800-218 v1.1, final, February 2022](https://csrc.nist.gov/pubs/sp/800/218/final) включает secure development practices; официальная project page отдельно отмечает security requirements/decisions и provenance release components. PlatformIO рекомендует pin development platform ([`platform`](https://docs.platformio.org/en/stable/projectconf/sections/env/options/platform/platform.html)) и описывает versioned package specifications ([`pio pkg install`](https://docs.platformio.org/en/stable/core/userguide/pkg/cmd_install.html)).

Кандидаты в gates:

- Configuration items включают requirements/hazard/architecture docs, source, PCB revision/assumptions, `platformio.ini`, board definitions, Arduino Core/platform/toolchain/libraries, analyzer configs, bootloader/keys policy, test bench и release metadata.
- Версии PlatformIO Core, platform, framework/toolchain и libraries фиксируются достаточно строго для воспроизводимости. Любое обновление зависимости проходит отдельный controlled change и regression/HIL.
- Release строится в чистой автоматизированной среде из tagged commit; сохраняются immutable binaries, map/size reports, manifest, digests/signatures, dependency/tool versions, test evidence, known limitations и approved deviations.
- Binary содержит диагностируемые product/version/build identifiers, однозначно связанные с source commit и release record; секретный signing key не в репозитории и не в обычном CI artifact.
- Release gate проверяет clean source provenance, review/approval, все обязательные checks, resource margins, update/rollback package и recovery instructions. Один и тот же утвержденный artifact продвигается между средами без rebuild.
- Change control требует impact analysis на requirements, architecture, hazards, tests, compatibility и field update path; emergency release сокращает lead time, но не authenticity, traceability и recovery tests.

## 4. B: справочные источники

| Источник | Полезное применение | Почему не принимать целиком |
|---|---|---|
| [ISO/IEC/IEEE 12207:2026](https://www.iso.org/standard/90219.html) | Карта полного lifecycle от conception до retirement, включая firmware, configuration, verification, operation и maintenance | Универсальный платный process framework; проекту требуется tailoring, а не весь набор процессов |
| [ISO/IEC 25010:2023](https://www.iso.org/standard/78176.html) | Проверка полноты quality requirements и общий словарь | Модель не задает проектные thresholds; их нужно получить из hazard/use context и измерить |
| [ISO/IEC/IEEE 42010:2022](https://www.iso.org/standard/74393.html) | Структура архитектурного описания и viewpoints | Не задает архитектуру, метод, нотацию или embedded scheduling rules |
| [MISRA C++:2023](https://misra.org.uk/product/misra-cpp2023/) | Современная база безопасного C++17 для будущего licensed mapping | Полный текст платный; Arduino Core/third-party code и tool coverage требуют scope/deviation strategy |
| [SEI CERT C++](https://wiki.sei.cmu.edu/confluence/display/cplusplus) | Открытые security-focused правила и rationale | Wiki прямо обозначен work in progress; версия 2016 не заменяет современный проектный профиль |
| [IEC 61508-1:2010](https://webstore.iec.ch/en/publication/5515), [часть 3](https://webstore.iec.ch/en/publication/5517), [часть 7](https://webstore.iec.ch/en/publication/5521) | Термины functional safety lifecycle, systematic capability, techniques/measures | Без product-sector decision, SIL и полного текста нельзя заявлять применение/соответствие |
| [IEC 60812:2018](https://webstore.iec.ch/en/publication/26359), [IEC 61025:2006](https://webstore.iec.ch/en/publication/4311) | Методическая проверка локальных FMEA/FTA templates после покупки | Открытые abstracts не содержат всей процедуры |
| [IEEE 1012-2024](https://standards.ieee.org/ieee/1012/7324/), [ISO/IEC/IEEE 29119 series](https://committee.iso.org/sites/jtc1sc7/home/projects/flagship-standards/isoiecieee-29119-series.html) | Полнота V&V/test processes, reviews и test design | Integrity-level/formal process tailoring может быть тяжелее нужного; HIL depth задается рисками V3 |
| [RFC 9019](https://www.rfc-editor.org/rfc/rfc9019.html), [RFC 9124](https://www.rfc-editor.org/rfc/rfc9124.html) | Threat-driven update architecture и manifest information | Оба Informational; заимствовать поля/решения, не заявлять protocol conformance |
| [NIST SP 800-193](https://csrc.nist.gov/pubs/sp/800/193/final) | Protection/detection/recovery и root-of-trust reasoning | Написан для platform firmware; конкретные NIST/FIPS controls могут не соответствовать MCU/product context |
| [ST SBSFU by MCUboot](https://wiki.st.com/stm32mcu/wiki/Security:SBSFU_by_MCUboot), [AN5447](https://www.st.com/resource/en/application_note/an5447-overview-of-secure-boot-and-secure-firmware-update-solution-on-arm-trustzone-stm32-microcontrollers-stmicroelectronics.pdf) | Feasibility и vendor-specific security mechanisms | MCU-family dependent; legacy X-CUBE-SBSFU заменен на новых сериях, интеграция с обязательным stack должна быть доказана |
| [ISO 17359:2018](https://www.iso.org/standard/71194.html) | Связь наблюдаемости firmware с condition-monitoring program машины | Не стандарт software observability и не задает event/log schema |
| [NIST SP 800-218 v1.1](https://csrc.nist.gov/pubs/sp/800/218/final) | Secure SDLC, provenance и protection of software | Framework требует risk-based tailoring; draft v1.2 не использовать как утвержденную основу |
| [PlatformIO stable docs 6.1](https://docs.platformio.org/en/stable/) | Реализуемые CI, dependency и host/target test capabilities | Документация инструмента, а не независимый стандарт качества |
| [Arduino Core STM32 PlatformIO wiki](https://github.com/stm32duino/Arduino_Core_STM32/wiki/PlatformIO) | Фактическая граница поддержки | STM32duino прямо говорит, что PlatformIO issues не поддерживаются им; риск принадлежит проекту/PlatformIO |

## 5. C: не применять как программу без дополнительного основания

- **Формальное соответствие IEC 61508, SIL assignment, safety case и tool qualification**: избыточно без заявленной safety function, отраслевого/договорного требования и компетентной независимой оценки. Полезные методы hazards, safe states, independence и evidence можно принять локально без слов «IEC 61508 compliant».
- **Полное соответствие MISRA C++:2023**: невозможно обосновать по странице-аннотации и отчету одного анализатора. Потребуются лицензированный текст, defined scope (включая Arduino Core/vendor/third-party code), documented deviations, coverage matrix и manual checks.
- **Полное соответствие ISO/IEC/IEEE 12207, 29148, 42010, 29119, IEEE 1012/828**: не является целью. Полные process/document sets создадут значительный overhead; принимать только локально выбранные outcomes и gates.
- **Автомобильные ISO 26262/AUTOSAR, авиационные DO-178C, медицинские IEC 62304, railway IEC 62279 и cybersecurity certification schemes**: domain-specific и неприменимы без соответствующего intended use/market/contract. Их упоминание не повышает assurance V3.
- **FIPS-validated cryptography или формальная квалификация static-analysis/test tools**: не требуются автоматически. Нужны только если это установит рынок, заказчик, threat model или certification target.
- **Буквальное внедрение полного SUIT protocol/CBOR manifest**: RFC 9019/9124 полезны как information model; конкретный wire format выбирается после оценки flash/RAM, bootloader и backend interoperability.
- **Condition-monitoring certification по ISO 17359**: firmware V3 лишь поставляет достоверные диагностические данные; общая программа мониторинга машины выходит за границы прошивки.

## 6. Предлагаемая последовательность превращения в gates

1. Утвердить project-specific tailoring record: scope, constraints, классы A/B/C, словарь и запрет certification claims.
2. Создать русскую модель требований с ID, quality budgets и двунаправленной трассировкой requirements → architecture/hazards → verification.
3. Провести FMEA фиксированной PCB и firmware states; определить safe/degraded states и top events для FTA.
4. Зафиксировать architecture viewpoints, scheduling/timing/resource budgets и test seams вокруг Arduino/STM32 APIs.
5. Утвердить локальный C++17 profile, static-analysis configuration и deviation workflow; отдельно решить судьбу third-party/framework findings.
6. Выполнить secure-update feasibility spike на точной модели STM32 и memory map до обещания OTA/rollback; выбрать vendor-supported boot chain либо документировать собственную trust boundary.
7. Определить verification matrix и HIL fault-injection campaign, включая power-cut update и safe-state timing.
8. Зафиксировать configuration/release baseline, reproducible build, artifact provenance/signing и production acceptance record.

## 7. Неустраненные решения и блокеры

- Не указаны точная модель STM32, flash/RAM, memory map, external storage, hardware revision и доступные security features. Без этого нельзя подтвердить dual-slot/swap, immutable root of trust, key protection и rollback feasibility.
- Не определены hazards объекта управления, physical safe state, acceptable response times и risk acceptance authority. Поэтому невозможно назначить конкретные safety/timing thresholds.
- Не определены update transport/backend, fleet identity, provisioning/manufacturing process, key custody/rotation/revocation и срок поддержки продукта.
- Обязательная комбинация Arduino Core STM32 + PlatformIO имеет разделенную границу поддержки: STM32duino официально не поддерживает обращения по PlatformIO. Версии платформы/framework/toolchain должны быть квалифицированы проектными тестами и зафиксированы.
- Не выбраны static analyzer и доступ к лицензии MISRA C++:2023. До этого нельзя построить достоверную rule/coverage/deviation matrix.
- Полные тексты ISO/IEC/IEEE, IEC и MISRA в исследовании не читались из-за paywall/licensing. Перед переносом конкретных clause-level требований в нормативные документы проекта нужен лицензированный primary text актуальной редакции.

## 8. Реестр первичных источников

Все URL проверены/прочитаны **2026-07-31**. Для web-документации без отдельной даты указана наблюдаемая версия или `без даты`.

| ID | Первичный источник | Редакция/дата источника | Доступ |
|---|---|---|---|
| S1 | ISO, [ISO/IEC/IEEE 29148](https://www.iso.org/standard/72089.html) | 2018, edition 2; отмечен как подлежащий пересмотру | Официальная аннотация; полный текст платный |
| S2 | ISO, [ISO/IEC/IEEE 42010](https://www.iso.org/standard/74393.html) | 2022-11 | Официальная аннотация; полный текст платный |
| S3 | ISO, [ISO/IEC/IEEE 12207](https://www.iso.org/standard/90219.html) | 2026-04 | Официальная аннотация; полный текст платный |
| S4 | ISO, [ISO/IEC 25010](https://www.iso.org/standard/78176.html) | 2023-11, edition 2 | Официальная аннотация; полный текст платный |
| S5 | MISRA, [MISRA C++:2023](https://misra.org.uk/product/misra-cpp2023/) | 2023-10; C++17 | Официальная страница; полный текст платный |
| S6 | SEI CERT, [C++ Coding Standard](https://wiki.sei.cmu.edu/confluence/display/cplusplus) и [Tool Selection](https://wiki.sei.cmu.edu/confluence/display/cplusplus/Tool+Selection+and+Validation) | Wiki без фиксированной редакции; официальный release 2016 указан на странице | Открыто; evolving guidance |
| S7 | IEC, [IEC 60812](https://webstore.iec.ch/en/publication/26359) | 2018-08-10, edition 3.0 | Официальная аннотация; полный текст платный |
| S8 | IEC, [IEC 61025](https://webstore.iec.ch/en/publication/4311) | 2006-12-13, edition 2.0 | Официальная аннотация; полный текст платный |
| S9 | IEC, [IEC 61508-1](https://webstore.iec.ch/en/publication/5515), [IEC 61508-3](https://webstore.iec.ch/en/publication/5517), [IEC 61508-7](https://webstore.iec.ch/en/publication/5521) | 2010-04-30, edition 2.0 | Официальные аннотации; полные тексты платные |
| S10 | NIST, [SP 800-193](https://csrc.nist.gov/pubs/sp/800/193/final) | Final, 2018-05-04 | Полный официальный PDF открыт |
| S11 | IETF/RFC Editor, [RFC 9019](https://www.rfc-editor.org/rfc/rfc9019.html) | 2021-04, Informational | Полный официальный текст открыт |
| S12 | IETF/RFC Editor, [RFC 9124](https://www.rfc-editor.org/rfc/rfc9124.html) | 2022-01, Informational | Полный официальный текст открыт |
| S13 | STMicroelectronics, [SBSFU by MCUboot](https://wiki.st.com/stm32mcu/wiki/Security:SBSFU_by_MCUboot) | без даты страницы; актуальная wiki на дату доступа | Открытая first-party wiki |
| S14 | STMicroelectronics, [AN5447](https://www.st.com/resource/en/application_note/an5447-overview-of-secure-boot-and-secure-firmware-update-solution-on-arm-trustzone-stm32-microcontrollers-stmicroelectronics.pdf) | application note, версия PDF на дату доступа | Полный first-party PDF открыт |
| S15 | ISO, [ISO 17359](https://www.iso.org/standard/71194.html) | 2018-01, edition 3; confirmed | Официальная аннотация; полный текст платный |
| S16 | IEEE, [IEEE 1012-2024](https://standards.ieee.org/ieee/1012/7324/) | Board approval 2024-11-12; published 2025-08-22 | Официальная аннотация; полный текст платный |
| S17 | ISO, [ISO/IEC/IEEE 29119-2](https://www.iso.org/standard/79428.html) и [официальный обзор серии](https://committee.iso.org/sites/jtc1sc7/home/projects/flagship-standards/isoiecieee-29119-series.html) | 2021-10, edition 2 | Официальные аннотации; полный текст платный |
| S18 | IEEE, [IEEE 828-2012](https://standards.ieee.org/ieee/828/10549/) | 2012; активный P828 готовит замену | Официальная аннотация; полный текст платный |
| S19 | NIST, [SP 800-218 v1.1](https://csrc.nist.gov/pubs/sp/800/218/final) | Final, 2022-02 | Полный официальный PDF открыт; v1.2 только draft на дату доступа |
| S20 | PlatformIO, [Unit Testing](https://docs.platformio.org/en/stable/advanced/unit-testing/introduction.html), [dependencies](https://docs.platformio.org/en/stable/librarymanager/dependencies.html), [`platform`](https://docs.platformio.org/en/stable/projectconf/sections/env/options/platform/platform.html), [`pio pkg install`](https://docs.platformio.org/en/stable/core/userguide/pkg/cmd_install.html) | stable docs 6.1 / 6.1.19 на дату доступа | Открытая официальная документация |
| S21 | stm32duino, [Arduino Core STM32](https://github.com/stm32duino/Arduino_Core_STM32), [PlatformIO wiki](https://github.com/stm32duino/Arduino_Core_STM32/wiki/PlatformIO), [PlatformIO build script](https://github.com/stm32duino/Arduino_Core_STM32/blob/main/tools/platformio/platformio-build.py) | main/repository state на дату доступа; wiki отмечает Core 2.8.x | Открытые first-party repository/docs |

## 9. Итог

Для V3 разумна небольшая **tailored assurance program**, а не имитация сертификации. Ее ядро: качественные трассируемые требования, concern-driven архитектура, локальный проверяемый C++17 profile, risk-driven FMEA/FTA и safe-state tests, authenticated power-fail-safe update с контролируемым rollback, bounded diagnostics, полная verification matrix до HIL и воспроизводимый подписанный release. ISO/IEC/IEEE/IEC/MISRA дают структуру и контрольные вопросы; открытые NIST/IETF/ST/PlatformIO материалы дают более непосредственно применимые технические критерии. Ни один из этих источников по отдельности не доказывает качество или соответствие продукта.
