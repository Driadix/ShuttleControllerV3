# Verification runner (bench tooling, ticket #65)

Production-реализация операторского цикла L4 (и будущего L5) по контракту
[issue #60](https://github.com/Driadix/ShuttleControllerV3/issues/60)
(`docs/operator-loop-design-v3.md`), дизайн - `docs/verification-runner-design-v3.md`.

Runner - host-инструмент стенда: исполняет сценарий вокруг физического стенда
(обнаружение платы/порта, gated flash, bounded UART capture, нормализация,
identity binding) и производит **evidence bundle**. Это **не** firmware и **не**
product behavior: runner не содержит веток, превращающих failed product
behavior в pass.

## Требования

- `.venv-pio312` (Python 3.11, pyserial 3.5 - `requirements.txt`)
- PlatformIO Core 6.1.19 (frozen, #51 §3) - только для flash-шага
- OpenOCD из пакетов PIO (`tool-openocd@3.1200.0`) - probe/flash
- git (identity рабочего дерева)

Установка зависимостей:

```text
.venv-pio312/Scripts/python.exe -m pip install -r bench/verification-runner/requirements.txt
```

## Одна команда на сценарий

Сценарии v1: `scenarios/uart-probe.json`, `scenarios/flash-boot-smoke.json`.

```text
# 1. Подпись владельца per-run (ОБЯЗАТЕЛЬНА для любого прогона `run`;
#    probe - физическое взаимодействие, жёсткий gate, #60 §0.3):
.venv-pio312/Scripts/python.exe bench/verification-runner/runner.py checklist --owner Driadix --out checklist.json

# 2. Прогон сценария:
.venv-pio312/Scripts/python.exe bench/verification-runner/runner.py run \
    bench/verification-runner/scenarios/flash-boot-smoke.json --checklist checklist.json
.venv-pio312/Scripts/python.exe bench/verification-runner/runner.py run \
    bench/verification-runner/scenarios/uart-probe.json --checklist checklist.json
```

Evidence bundle пишется в `out/<scenario>-<timestamp>/` (raw-захват, result,
checklist). `out/` в `.gitignore`.

## CLI и exit-коды

```text
runner.py detect [--port COM9] [--no-uart]        # read-only диагностика (без gate)
runner.py checklist --owner NAME [--out FILE]     # подпись владельца (HITL)
runner.py run <scenario.json> [--port P] [--checklist F] [--out-dir D] [--owner NAME]
runner.py normalize <raw.bin>                     # raw -> machine-readable
runner.py evidence <result.json>                  # повторная проверка полноты bundle
```

| Exit | Вердикт | Условие |
| --- | --- | --- |
| 0 | PASS | evidence полон + oracle удовлетворён |
| 1 | FAIL | evidence полон, oracle нарушен |
| 2 | TIMEOUT | evidence полон, oracle не удовлетворён за окно |
| 3 | INCOMPLETE | evidence не может быть полным - отказ (до side effects) |
| 4 | SCHEMA ERROR | сценарий невалиден, прогон не начат |

**Инвариант отказов**: при неполном evidence (нет checklist-подписи, probe
FAIL, identity mismatch, uart port закрыт, нет gitSha/artifact sha256, flash
fail) физические операции с side effects - probe, UART port check, flash,
capture - НЕ исполняются: отказ предшествует side effect'у.

**Checklist-подпись проверяется по форме**: пред-подписанный файл обязан
содержать `schemaVersion: 1`, `signed: true`, `owner`, `at` и непустой
`items[]` с каждым `confirmed: true`; подделка вида `{"signed": true}` или
нечитаемый JSON дают refusal (exit 3) до любых физических операций.

## Identity-контракт (bench identity)

Каждый сценарий объявляет ожидаемую плату стенда в `identity.board`
(`part` + 96-bit `uid`). Runner сравнивает с фактическим probe
(DBGMCU_IDCODE + UID через OpenOCD) и при mismatch даёт `INCOMPLETE`
(`boardIdentityMismatch`) с эмиссией observed (`board`) и expected
(`boardExpected`) идентичностей; flash/capture не вызываются.

UID текущего стенда: `002900363033470336363131` (STM32F405RG,
idcode `0x100f6413`, замер 2026-08-13). При замене платы обновить
`identity.board` в сценариях (значение берётся из `runner.py detect`).

## Тесты (host, без железа)

```text
cd bench/verification-runner
../../.venv-pio312/Scripts/python.exe -m unittest discover -s tests -t .
```

Кейсы T1-T17: normalize (valid/resync/badcrc), oracle (PASS/TIMEOUT/FAIL),
gates (checklist обязателен для любого прогона; отказ блокирует
probe/UART/flash/capture; проверка формы checklist - forged/malformed),
identity (missing identity, board mismatch, toolchain record
{platformio, platform, core}), exit-коды, evidence bundle, schema
(невакуумность behavior, ограничения flash-verify, drift-guard против
сценария, exit 4, slug id).

## Bench facts (стенд #73)

- UART-путь: COM9 (relay-дисплей), 230400 8E1
- Кадр: `0xBB 0xCC | msgID | targetID | seq | length | payload | CRC16-CCITT`
  (max payload 120 B)
- MSG_LOG = 0x10 - строки для regex-oracle
- ST-Link V2 на XT21; probe перед повторным запуском убивает stale OpenOCD

## Split владелец/агент (#60 §4.3)

| Шаг | Исполнитель |
| --- | --- |
| Физическое подключение, energizing, checklist-подпись | владелец |
| Probe/flash/capture/normalize/verdict/evidence | runner (агент) |
