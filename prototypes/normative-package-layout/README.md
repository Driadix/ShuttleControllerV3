# THROWAWAY PROTOTYPE - схема нормативного пакета V3

Это одноразовый прототип для issue `Спроектировать репозиторную схему нормативного пакета`.
Он не является production-инструментом и не создаёт нормативные документы.

Запуск из корня репозитория:

```powershell
python prototypes/normative-package-layout/prototype.py
```

Прототип сравнивает два layout-кандидата, затем в памяти проверяет:

- typed immutable IDs и отсутствие collision/reuse;
- разрешённые пары и направление semantic relations, ownership shape и отсутствие циклов;
- восстановление records из `Design Baseline` manifest;
- gate approval record с checklist version, issue locator и commit locator;
- generated traceability/reverse-link view и staged coverage diagnostics;
- candidate impact closure для semantic change;
- representative slices для capability contract, safety invariant, architecture decision, verification obligation и implementation coverage.

Предварительный вердикт прототипа: выбрать concern-first layout (вариант B). Logical item остаётся видимой границей revision, approval и change impact, а typed Trace Records живут внутри item и не теряются в общей папке по типам.
