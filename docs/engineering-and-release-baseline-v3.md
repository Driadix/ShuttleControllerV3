# Инженерный baseline и выпуск V3 (Engineering & Release, item 11)

Статус: **утверждено владельцем (гриллинг, тикет [#51](https://github.com/Driadix/ShuttleControllerV3/issues/51))**. Вход в logical item `Engineering & Release` нормативного пакета (issue 8, gate G5) и в architecture proving slice (тикет #54). Каждый численный или версионный факт имеет источник; воспроизводимость проверяема командами раздела «Проверяемость».

Production-каркас (CI workflow, скрипты enforcement, `.clang-tidy`, PR-шаблоны) материализуется после достижения Destination карты (правило карты: PlatformIO-каркас вне планирующей карты), в implementation-карте. Исключение - минимальный исполняемый каркас proving slice #54: #54 - тикет текущей карты и pre-Destination evidence (issue 10: harness готовится до реализации capability functions, включает frozen toolchain/board bring-up); `platformio.ini` с пинами раздела 3, env kernel variants и native test env создаются в рамках #54 и эмпирически верифицируют пины раздела 3 до G4/G5. Настоящий документ задаёт обязательное содержимое обоих уровней; ссылки на `tools/*` и jobs в разделах 7-13 являются спецификацией production-каркаса.

## 1. Назначение и входы

Документ фиксирует engineering baseline V3: build-путь, frozen toolchain, board baseline, структуру репозитория и dependency rules, coding profile, static analysis, host-тесты, CI, версионирование, release и artifact provenance.

Входы: [#43](https://github.com/Driadix/ShuttleControllerV3/issues/43) (ports-and-adapters, раздельная сборка domain core), issue 10 (proving slice: три kernel variants, frozen toolchain обязателен), V1-индекс (production-вход Arduino IDE, team-approved settings не зафиксированы), [#8](https://github.com/Driadix/ShuttleControllerV3/issues/8) (item 11, gate G5).

## 2. Build-путь: PlatformIO-only

V3 production-сборка выполняется только через PlatformIO Core. Arduino IDE выводится из production-пути: CI, host-сборка домена, три kernel variants proving slice и artifact provenance требуют скриптуемой единой сборки; Arduino IDE не может их обеспечить. V1-режим «Arduino IDE = production, PlatformIO = compile check» (V1 README) в V3 не применяется.

## 3. Frozen toolchain

Пины зафиксированы в `platformio.ini` и в CI; версии проверяются командой `pio pkg list` (сверка с таблицей ниже) и `pio pkg outdated` (контроль дрейфа; lockfile в PlatformIO отсутствует - feature request [platformio-core#4613](https://github.com/platformio/platformio-core/issues/4613), поэтому пиннинг точными версиями является механизмом воспроизводимости).

| Пакет | Пин | Соответствие |
| --- | --- | --- |
| PlatformIO Core (pip) | `platformio==6.1.19` | последний stable на дату утверждения |
| platform | `ststm32@17.4.0` | пакет платформы; соответствует V1-пину (V1 `platformio.ini`) |
| toolchain-gccarmnoneeabi | `1.120301.0` | GCC 12.3.1 (xPack); принудительно для Arduino platform.py ststm32 v17.4.0 |
| framework-arduinoststm32 | `4.20701.0` | STM32duino core 2.7.1 (platform.txt `version=2.7.1`); тот же core-поколение, что team-approved V1 |
| framework-cmsis | `2.50900.0` | CMSIS-пакет платформы |
| tool-scons | `4.40801.0` | SCons build engine |
| tool-openocd | `3.1200.0` | ST-Link программирование (протокол stlink) |
| tool-dfuutil | `1.11.0` | DFU-загрузка (не production-путь V3) |
| tool-dfuutil-arduino | `1.11.0` | DFU-загрузка Arduino-рецептом (upload-таргеты) |
| tool-stm32duino | `1.0.2` | STM32duino uploader |

Пакеты, не перекрытые пином (резолв «последняя в диапазоне»), в V3 отсутствуют: таблица покрывает всё дерево ststm32@17.4.0 + arduino (эмпирическая проверка в чистом окружении, 2026-08-11).

Синтаксис пинов:

```ini
[env:firmware]
platform = ststm32@17.4.0
platform_packages =
  toolchain-gccarmnoneeabi@1.120301.0
  framework-arduinoststm32@4.20701.0
  framework-cmsis@2.50900.0
  tool-scons@4.40801.0
  tool-openocd@3.1200.0
  tool-dfuutil@1.11.0
  tool-stm32duino@1.0.2
```

Политика обновления: любое изменение пина (core, платформы, пакетов, runner-образа) классифицируется как **Semantic** change по issue 8 и требует independent review + impact-анализ; обновления выполняются осознанно, не автоматически. Допустимые причины: исправление дефекта toolchain, поддержка новой возможности core, устранение CVE.

## 4. Board baseline и build flags

- **Board**: `genericSTM32F405RG` (stock-определение платформы ststm32@17.4.0; кастомный board JSON не вводится, пока proving slice не докажет необходимость).
- **Upload/debug**: `upload_protocol = stlink`, `debug_tool = stlink` (ST-Link; production-прошивка в V1 также шла через ST-Link).
- **Frozen build flags** (наследуются из V1 `platformio.ini`, являются аппаратным контрактом PCB): `-D NO_HW_SERIAL`, `-D HAL_UART_MODULE_DISABLED`, `-D ARDUINO_GENERIC_F405RGT`, `-D HAL_CAN_MODULE_ENABLED`.
- **Язык и флаги компиляции**: ядро 2.7.1 компилирует `-std=gnu++17`, `-fno-exceptions`, `-fno-rtti`, `-fno-threadsafe-statics`, `-ffunction-sections`, `-fdata-sections` (platform.txt core 2.7.1). Host-сборка домена зеркалит те же флаги (включая `-std=gnu++17`, `-fno-exceptions`, `-fno-rtti`) для идентичности семантики.
- **Ограничение**: точные значения «team-approved» board menu Arduino IDE V1 не зафиксированы (V1-индекс, Unknown) - расхождение PIO board def с фактическим поведением V1-прошивки на плате является **validation obligation** первого hardware bring-up / proving slice (#54). При доказанном расхождении допускается board JSON override с impact-анализом.

## 5. Структура репозитория и dependency rules

Разделение по портам-and-adapters (#43):

| Корень | Содержимое | Зависимости |
| --- | --- | --- |
| `domain/` | 8 domain-компонентов (#43), порты, типы | только стандартная библиотека C++ (без динамической аллокации), никаких Arduino/RTOS/третьих сторон |
| `adapters/` | HAL, Transport, Persistence, Observability Sink | реализуют доменные порты; единственное место для Arduino Core и lib_deps |
| `platform/` | точки входа (main/setup), execution core (kernel variants), board init | склейка домена и адаптеров |
| `tests/` | host-тесты домена (GoogleTest) | googletest (dev-only) |
| `tools/` | скрипты enforcement (include-lint и др.) | - |

### Enforcement dependency rules

1. **Механический**: domain core собирается как native-библиотека без framework (`env:native`) - невозможность собрать без Arduino/RTOS является неопровержимым доказательством независимости.
2. **Include-lint** (`tools/check_includes.py`, запускается в CI): в исходниках `domain/` запрещены любые заголовки Arduino Core (включая `Arduino.h`, `Wire.h`, `HardwareSerial.h`, `SPI.h`, `CAN.h`), RTOS API и третьих сторон; в `domain/` запрещены типы с динамической аллокацией (`vector`, `string`, `map`, `list`, `deque`, `set` и т.п.) - допускаются `array`, `span`, `string_view` и фиксированные буферы.
3. **Dependency-матрица**: направление зависимостей между domain-компонентами фиксируется в разделе «Границы» документа #43; review-checklist требует подтверждения соответствия каждому PR.
4. **lib_deps**: допускаются только в adapter ring, только точные пины (`owner/name@x.y.z`), лицензии фиксируются в `docs/dependencies.md`. V1-библиотеки (AS5600 0.4.1, STM32_CAN 1.1.0, STM32_TimerInterrupt 1.3.0, STM32duino RTC 1.6.0) являются кандидатами на замену собственными адаптерами; решение - на proving slice (#54) в рамках #43, политика лицензирования действует независимо. **Контролируемое исключение**: env-scoped пакеты kernel variants proving slice (раздел 12, например ядро RTOS) размещаются на уровне env/platform, пинятся точными версиями по политике раздела 3, не затрагивают domain core и не являются адаптерами.

## 6. Coding profile V3

Внутренний профиль (решение владельца в гриллинге #51): нормируются конкретные правила и их enforcement, без привязки к платному внешнему стандарту. Стандарт языка - C++17 (gnu++17 на target и host). Профиль - нормативный перечень, проверяемый инструментами и review-checklist.

### 6.1 Категории правил

| # | Категория | Ключевые правила | Enforcement |
| --- | --- | --- | --- |
| R1 | Память | без динамической аллокации в домене после init; без `new`/`delete`; контейнеры только фиксированной ёмкости; zero heap post-init (#48) | include-lint, clang-tidy (`cppcoreguidelines-owning-memory`, `cppcoreguidelines-no-malloc`), review |
| R2 | Конкурентность/ISR | ISR пишет только в свой bounded ring (#43); без блокировок (один поток исполнения); `volatile` только для HW-регистров/ISR-флагов с комментарием | clang-tidy (`concurrency-*`, `bugprone-misplaced-widening-cast` не применим; `cert-*`), review |
| R3 | Типы | fixed-width типы (`stdint`) всегда; запрет неявных сужений; явные касты; packed-структуры протокола документируются | clang-tidy (`cppcoreguidelines-narrowing-conversions`, `bugprone-*`), `-Wconversion` на host |
| R4 | Поток управления | без `goto`; без рекурсии (bounded steps); `switch` с `default`; все циклы с bound (доказуемый выход) | clang-tidy (`bugprone-*`, `misc-*`), review (boundedness - входит в доказуемость #54) |
| R5 | Ошибки | без исключений; типизированные outcomes/error-коды (semantic contract); запрет молчаливого проглатывания ошибок; `assert` vs runtime-check политика: инварианты - assert (debug), внешние условия - типизированные outcomes | clang-tidy, review |
| R6 | Глобальное состояние | single-writer ownership (#43); запрет мутабельных глобалов в домене вне владельцев; snapshot-чтение | clang-tidy (`cppcoreguidelines-avoid-non-const-global-variables`), review |
| R7 | UB и портируемость | запрет UB (cert-проверки); implementation-defined поведение только с комментарием-обоснованием | clang-tidy (`cert-*`, `clang-analyzer-*`), cppcheck |
| R8 | Заголовки | include-what-you-use; `SortIncludes` (clang-format) | clang-format, review |

### 6.2 Формат

`clang-format` конфигурация наследуется из V1 `.clang-format` (LLVM-база, Allman, ColumnLimit 125, SortIncludes). Изменения конфигурации - Semantic change. Версия инструмента пинится в CI (clang-format 18).

### 6.3 Review-checklist (обязателен для каждого PR с кодом)

1. Зависимости: нет Arduino/RTOS/3rd-party include в `domain/`; направление зависимостей по матрице #43.
2. Память: нет динамической аллокации в домене; bounded-структуры.
3. ISR: только bounded ring; нет policy в ISR (#43).
4. Ошибки: все ошибки типизированы; нет молчаливых веток; outcomes по semantic contract.
5. Single-writer: мутации только у владельца; кросс-доступ snapshot'ами.
6. Циклы bounded; без рекурсии; без goto.
7. Портируемость: fixed-width типы; нет UB; implementation-defined с комментарием.
8. Инварианты safety-модели (#45) не ослаблены; force-stop путь не в очередях.

## 7. Static analysis и linters

| Инструмент | Версия (CI) | Команда | Назначение |
| --- | --- | --- | --- |
| clang-format | 18 (apt ubuntu-24.04) | `clang-format --dry-run --Werror` по `domain/ adapters/ platform/ tests/` | формат |
| clang-tidy | 18 (apt ubuntu-24.04) | `pio run -t compiledb` затем `clang-tidy -p <build> --header-filter='^(domain\|adapters\|platform)/' domain/ adapters/ platform/` | static analysis |
| cppcheck | apt ubuntu-24.04 (2.13) | `cppcheck --enable=warning,performance,portability --std=c++17 --error-exitcode=1 --inline-suppr --suppress=missingIncludeSystem --suppress=unmatchedSuppression --force --quiet domain/ adapters/ platform/` | static analysis (наследует V1-скрипт) |
| markdownlint | markdownlint-cli2 (npm, pin) | `markdownlint-cli2 docs/**/*.md CONTEXT.md` | docs lint |
| CodeQL | github/codeql-action (SHA-pin) | default + security-extended, языки: cpp | security analysis |

**clang-tidy check set** (нормативный; конфигурация в `.clang-tidy`):

- Включены группы: `bugprone-*`, `cert-*`, `clang-analyzer-*`, `concurrency-*`, `performance-*`, `portability-*`.
- Выбранные: `cppcoreguidelines-avoid-non-const-global-variables`, `cppcoreguidelines-init-variables`, `cppcoreguidelines-narrowing-conversions`, `cppcoreguidelines-no-malloc`, `cppcoreguidelines-owning-memory`, `cppcoreguidelines-pro-type-member-init`, `misc-misleading-identifier`, `misc-static-assert`, `misc-unused-*`, `readability-misleading-indentation`, `readability-static-accessed-through-instance`.
- Исключены: `modernize-*` (churn), `google-*`, `llvm-*`, `readability-*` шумные (identifier-naming, magic-numbers), `hicpp-*` (удалён из upstream за лицензионные причины).
- Внимание: upstream clang-tidy не содержит MISRA/AUTOSAR-модулей (факт на 2026-08-11); профиль построен на эквивалентных checks без внешнего стандарта.

**Отклонено**: PVS-Studio (покрытие MISRA C++:2023 35% M+R, платный; free-лицензия только для личных некоммерческих OSS - наш проект не подходит); сторонние alpha-паки MISRA для clang-tidy (нестабильны).

## 8. Host-тесты домена

- **Окружение**: `[env:native]`, `platform = native` (без framework; системный GCC хост-ОС). В CI - пинированный runner `ubuntu-24.04` (системный GCC версии образа документируется в workflow), локально - любой хост с GCC в PATH (Windows: MSYS2, документируется в README).
- **Фреймворк**: GoogleTest (`test_framework = googletest`); точная версия пакета фиксируется в `platformio.ini` при первом стендапе native env (framework пакета platform-native) и попадает под политику пинов раздела 3.
- **Запуск**: `pio test -e native` (JUnit/JSON-отчёты для CI); target-тесты (embedded) - вне scope host-слоя, относятся к verification pyramid (#52).
- **Охват**: host-deterministic тесты домена: unit + property (property-тесты специфицируются в #52); deterministic core (#10) - обязательное условие host-тестируемости.
- Верификация: тесты запускаются в CI на каждом PR; локально - та же команда.

## 9. CI

### 9.1 PR-workflow (pull_request + push в main): `ci.yml`

| Job | Шаги |
| --- | --- |
| toolchain | `pio pkg list` сверяется с таблицей пинов раздела 3 скриптом `tools/check_toolchain.py` (несоответствие = красный PR) |
| docs | markdownlint-cli2 |
| format | clang-format --dry-run --Werror |
| static | cppcheck + clang-tidy (compiledb) |
| host-tests | `pio test -e native` (GoogleTest) |
| build | `pio run -e firmware` (target-сборка) |
| codeql | CodeQL default + security-extended (cpp) |

Общие требования:

- Runner: `ubuntu-24.04` (пин, не `latest`).
- Actions: **SHA-pinning** всех сторонних actions с комментарием версии (например `actions/checkout@<sha> # v4.x.y`); обновления - Dependabot, каждое обновление проходит review (Semantic-класс по issue 8).
- Python: `actions/setup-python@<sha> # v6`, python 3.11; PlatformIO Core: `pip install platformio==6.1.19`.
- Кэш: `~/.cache/pip` + `~/.platformio/.cache` через `actions/cache@<sha> # v4`, ключ по hash `platformio.ini`.
- Env: `CI=true`, `PLATFORMIO_DISABLE_UPGRADE_CHECK=true`.

### 9.2 Release-workflow (тег `v*`): `release.yml`

1. Сборка `env:firmware` (те же пины).
2. Формирование версии из тега и SHA (раздел 10).
3. `sha256sum` манифест.
4. Draft release (`gh release create --draft`), attach `firmware.elf`, `firmware.hex`, `firmware.bin`, `SHA256SUMS.txt`.
5. Attestation: `actions/attest@<sha> # v4` с `subject-path` на бинарники (раздел 11).
6. Publish draft (immutable releases best practice: draft -> assets -> publish).

Permissions release-workflow: `contents: write`, `id-token: write`, `attestations: write`; триггер - только на тег `v*` (tag protection включён).

## 10. Версионирование

- **Firmware version**: semver-тег `vX.Y.Z`; в образ встраивается `FW_VERSION_STR` (из тега) и `FW_GIT_SHA` (из коммита) через build flags. Локальные dev-сборки: `git describe --tags --dirty` fallback.
- **Воспроизводимость**: build timestamp в образ не встраивается; `SOURCE_DATE_EPOCH` не используется (артефакты детерминированы пинами раздела 3).
- Версия прошивки ортогональна версии протокола (#47) и версии конфигурации/профилей (#50); связи фиксируются в контрактах handshake (#47).

## 11. Artifact provenance

- **Механизм**: GitHub Artifact Attestations (GA): `actions/attest@v4` (обёртка `attest-build-provenance` deprecated для новых проектов), SLSA v1.0 Build Level 2, Sigstore (публичный репозиторий - Public Good Instance).
- **Субъекты**: `firmware.elf`, `firmware.hex`, `firmware.bin` (subject-path); манифест `SHA256SUMS.txt` в формате shasum.
- **Верификация потребителем** (документируется для OTA-флоу #50):
  - `gh attestation verify firmware.bin -R Driadix/ShuttleControllerV3`
  - `gh release verify vX.Y.Z` / `gh release verify-asset vX.Y.Z firmware.bin`
- Immutable releases включены (автоматическая release attestation).
- Ограничение: attestations доступны на публичных репозиториях (наш - публичный); private/Enterprise-перенос потребует Enterprise Cloud.

## 12. Поддержка proving slice (#54)

Frozen toolchain (раздел 3) разблокирует #54. Harness #54 исполняется на текущей карте (pre-Destination evidence, issue 10); его минимальный исполняемый каркас (platformio.ini с пинами, kernel envs, native test env) - исключение из правила «каркас после Destination» (раздел 1). Требования baseline к harness:

- Три kernel variants (cooperative / hybrid / static RTOS) выражаются как **отдельные env** одного дерева исходников: общий domain core + `platform/execution_<variant>.cpp` точки входа; без изменения домена.
- Каждый env наследует пины раздела 3; RTOS-variant добавляет свои пакеты с пинами (политика раздела 3).
- Host-леги `pio test -e native` для всех трёх variants (сравнительный отчёт #54) - один native env, kernel выбирается конфигурацией сборки.
- Измерения на target - вне этого документа (методика #54, бюджеты #48, дедлайны #45).

## 13. Проверяемость (closure predicates item 11, gate G5)

| Требование G5 | Проверка |
| --- | --- |
| toolchain воспроизводим | CI job `toolchain`: `tools/check_toolchain.py` сверяет `pio pkg list` с пинами раздела 3; `pip install platformio==6.1.19`; runner ubuntu-24.04 |
| coding политика воспроизводима | `.clang-format` + `.clang-tidy` в репозитории; review-checklist раздела 6.3 в PR-процедуре |
| static-analysis воспроизводима | команды раздела 7 в `ci.yml` (job static); версии инструментов в workflow |
| CI воспроизводим | `ci.yml` + SHA-пины actions; кэш по hash platformio.ini |
| release политика воспроизводима | `release.yml` (draft -> attest -> publish); версия из тега; манифест SHA256; перед релизом - контроль `pio pkg outdated` (ответственный: релиз-менеджер/владелец) |
| provenance фиксируется | `actions/attest@v4` в release-workflow; проверка `gh attestation verify` задокументирована |

## 14. Assumptions / Unknowns / Confidence

- **Assumption**: stock board def `genericSTM32F405RG` воспроизводит поведение V1-прошивки, собранной в Arduino IDE (точные board menu V1 неизвестны). **Validation obligation** первого bring-up / #54.
- **Assumption**: gnu++17 (core 2.7.1) приемлем как стандарт языка; host-зеркало флагов даёт идентичную семантику.
- **Unknown**: точные версии пакетов googletest framework при стендапе native env - фиксируются в platformio.ini по факту (политика пинов применяется с момента фиксации).
- **Confidence**: высокая для механизмов pinning и CI (эмпирически проверено в чистом окружении, факт-шит 2026-08-11); поведенческая эквивалентность PIO vs Arduino IDE - ниже, закрывается hardware bring-up.

## 15. Условия пересмотра

- Расхождение PIO-сборки с поведением V1 на плате (bring-up/#54) - board JSON override или восстановление Arduino IDE настроек, с impact-анализом.
- Требование сертификации/внешнего аудита - пересмотр coding profile в сторону внешнего стандарта (MISRA C++:2023 subset + PVS-Studio).
- Повторяющиеся классы дефектов, не ловящиеся review-checklist - усиление профиля/инструментов.
- Обновление core/платформы (новые возможности, CVE) - только по политике раздела 3 с review.

## 16. Отклонённые альтернативы

- **Гибрид Arduino IDE + PIO**: два параллельных build-пути с разными флагами; невозможность CI/provenance/host-тестов как gate. Отклонено (владелец).
- **MISRA C++:2023 subset**: платный текст, OSS-enforcement частичный (clang-tidy без MISRA-модулей, cppcheck misra = C only, PVS 35% M+R), overhead процесса без требования сертификации. Отклонено (владелец): принят внутренний профиль с эквивалентными checks. Пересматривается при смене цели на сертификацию.
- **MISRA C++:2008**: устаревшая база C++03; alpha-паки для clang-tidy нестабильны. Отклонено.
- **AUTOSAR C++14**: официально retired (передан в MISRA C++:2023). Отклонено.
- **Doctest / plain asserts** для host-тестов: меньше возможностей, чем GoogleTest (параметризация, mocking, отчёты). Отклонено (владелец).
- **Major-теги actions**: мутабельные теги в release-пути. Отклонено (владелец): SHA-pinning.
- **CodeQL deferred**: дополнительный бесплатный слой поверх clang-tidy/cppcheck для публичного репозитория. Отклонено (владелец): включён.

## 17. Ссылки

- `platformio.ini` V1 (пин `ststm32 @ 17.4.0`), README V1 (production Arduino IDE).
- Факт-шиты (исследование, 2026-08-11): pinning PlatformIO (docs.platformio.org, platform-ststm32 v17.4.0 platform.json/platform.py, registry.platformio.org); coding standards (misra.org.uk, clang.llvm.org, cppcheck addons README, pvs-studio.com); provenance/CI (docs.github.com artifact attestations / immutable releases, actions/attest README, docs.platformio.org CI).
- Входы: #43 (границы), issue 10 (proving slice), #48 (бюджеты), #45 (safety), #8 (item 11, G5), V1-индекс (`a7f927c`).
