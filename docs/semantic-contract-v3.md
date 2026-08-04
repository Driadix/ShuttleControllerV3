# Общий semantic contract операций V3

Статус: утверждено по issue [«Специфицировать общий semantic contract операций V3»](https://github.com/Driadix/ShuttleControllerV3/issues/13).

## Назначение и границы

Этот документ задаёт общий нормативный смысл запросов операций, экземпляров операций и их внешней видимости. Он одинаков для всех operation types V3 и не зависит от transport profile. Транспорт кодирует этот контракт, но не изменяет его смысл.

Документ не задаёт wire format, конкретные enum-коды safety/fault, количественные resource budgets или execution engine. Алгоритмический baseline операций сохраняется по решению каталога V3: execution shape меняется на неблокирующий, но функциональный и наблюдаемый результат сохраняется, если отдельным решением не установлен другой disposition.

## Канонические объекты

### Operation request

`Operation request` - предложение авторизованного источника создать один operation instance. Запрос ещё не является операцией и не резервирует ресурсы до завершения admission.

Новый логический запрос обязан иметь уникальный `requestId` в пределах `(controllerEpoch, authorityId)`. Повторная доставка того же запроса использует тот же `requestId`; создание нового логического запроса требует нового `requestId`.

### Operation type

`Operation type` - нормативное описание доменного действия: допустимые роли root и child, параметры, preconditions, resource set, lifecycle, outcomes, cancellation/stop behavior, retry/idempotency и link-loss policy.

Общий envelope содержит расширяемое поле `parameters`. Его содержимое и обязательность определяет контракт конкретного operation type. На этом этапе не добавляются `MaxIterations`, progress counters, percentage progress или эквивалентные policy inputs.

### Operation instance

`Operation instance` - принятое firmware предложение с собственной identity и lifecycle. Firmware создаёт `operationId` только после успешного admission.

Экземпляр имеет:

- `operationId` - непрозрачный идентификатор, уникальный как минимум в пределах `controllerEpoch`;
- `operationType`;
- `parentOperationId` - обязательный для suboperation и отсутствующий у root;
- `authority` - роль и identity владельца запроса или родительского operation context;
- `controllerEpoch`;
- lifecycle state и terminal outcome, если экземпляр завершён.

### Root, suboperation и primitive

- `Root operation instance` создаётся из принятого внешнего либо внутреннего запроса и не имеет `parentOperationId`.
- `Suboperation` - operation instance, которым владеет составная операция. Он создаётся только по разрешённому ребру статического type graph, получает делегированные ресурсы и не может приобрести ресурсы за пределами delegation.
- `Primitive` - неделимое действие внутри operation algorithm. У primitive нет собственной operation identity, lifecycle или внешнего ACK.

Статический graph типов конечен и ацикличен. Runtime active instances образуют дерево владения. `parentOperationId` не может образовать прямой или косвенный цикл.

## Обязательный request envelope

Для запроса, который может создать operation instance, обязательны:

| Поле | Смысл |
| --- | --- |
| `requestId` | Identity логического запроса источника; сохраняется при retry той же доставки. |
| `controllerEpoch` | Точное непрозрачное значение, выданное этим контроллером и последнее наблюдённое клиентом. WMS/Control Client возвращает его без преобразования; запрос с отсутствующим или отличающимся epoch отклоняется до создания operation instance. Для внутренних Safety Authority запросов firmware подставляет текущий epoch. |
| `authority` | Каноническая роль (`Control Client`, `Service Client` или `Safety Authority`) и identity principal. Transport endpoint не заменяет authority. |
| `operationType` | Тип создаваемой операции. |
| `parameters` | Объект параметров, валидируемый operation type; пустой объект допустим, если тип не требует параметров. |
| `parentOperationId` | Обязателен только для child request и должен ссылаться на активный parent, владеющий допустимым delegation. |

`operationId` не принимается от внешнего клиента как identity операции. Поля wire encoding, timestamp и channel-specific correlation остаются ответственностью transport profile при сохранении этих semantic obligations.

`controllerEpoch` публикуется контроллером в актуальном handshake, query snapshot и ACK. Внешний клиент, включая WMS, хранит последнее полученное значение и включает его в каждый mutating operation request. Клиентский или WMS-generated epoch является отдельным transport/application concept и не может подставляться вместо `controllerEpoch`.

## Admission и ACK

Admission является атомарной границей между request и operation instance. До ACK firmware последовательно проверяет:

1. envelope, epoch, authority и права operation type;
2. schema и значения `parameters`;
3. operation preconditions и safety permit;
4. допустимость parent/child composition и delegation;
5. idempotency/conflict semantics;
6. доступность и эксклюзивность требуемых ресурсов;
7. создание operation instance и фиксацию его начального lifecycle.

Ресурсы root резервируются до положительного ACK. Child резервирует только делегированный parent набор. Отрицательный ACK не создаёт operation instance и не оставляет частичной resource reservation.

Положительный ACK содержит как минимум `requestId`, `controllerEpoch`, `operationId`, `operationType`, `parentOperationId` (или null для root) и начальное состояние `Accepted`. Отрицательный ACK содержит `requestId`, `controllerEpoch` и стабильный rejection code, но не `operationId`.

## Idempotency, conflict и retry

Firmware ведёт bounded in-memory idempotency ledger, индексированный по `(controllerEpoch, authorityId, requestId)`. Ledger хранит admission result и связанный `operationId`, если запрос был принят. Persistence ledger через reboot не требуется на текущем этапе.

- Повтор того же ключа с тем же semantic payload возвращает тот же admission result и не создаёт новый instance.
- Тот же ключ с отличающимся `operationType`, `parentOperationId`, authority или payload получает `Conflict`; новый instance не создаётся.
- Запрос с уже занятым ресурсом получает `ResourceConflict`; ACK не выдаётся до успешной reservation.
- Retry после transport timeout обязан использовать тот же `requestId`, пока клиент ожидает, что запись есть в bounded ledger.
- После reboot старый epoch не принимается. WMS/клиент обязан получить новый epoch через handshake или query и только после этого отправлять новые mutating requests. Если запись вытеснена из bounded ledger, protocol больше не обещает распознать повтор; для необратимых действий клиент обязан сначала сверить состояние через query/snapshot и только затем отправлять новый логический запрос с новым `requestId`.

Точная ёмкость, retention window и политика поведения при исчерпании ledger входят в будущие quantitative NFR и verification gates. Они не меняют identity tuple и правило «один логический запрос - один requestId».

## Lifecycle и outcomes

Admission result `Rejected` относится к request и не является lifecycle state operation instance. Принятый instance использует общий lifecycle:

```text
Accepted -> Running -> Succeeded
Running  -> Stopping -> Cancelled
Running  -> Failed
Stopping -> Failed       (если safe stop не может завершиться)
```

`Succeeded`, `Cancelled` и `Failed` являются terminal states. Terminal outcome не изменяется повторной доставкой request. Outcome содержит стабильный typed code и минимальный диагностический context; полный safety/fault taxonomy определяется отдельными нормативными решениями.

Operation type задаёт link-loss policy: `continue`, `controlled_stop` или `fail_safe_completion`. Политика является частью type contract, а не произвольным request parameter. Manual Control Session имеет отдельный lease contract и не подменяется этим lifecycle.

## Cancellation и stop propagation

Cancellation/stop является control intent над существующим `operationId`, а не новым operation type. Повторный stop для того же instance идемпотентен. После принятия stop:

- root переводится в `Stopping` и распространяет stop на все активные descendants;
- parent остаётся `Stopping`, пока descendants безопасно не завершены и delegated resources не освобождены;
- child не может продолжать доменный шаг после отзыва delegation;
- Safety Authority может применить safety precedence по отдельному safety contract, не нарушая identity и traceability текущего instance.

Точный safety outcome при конфликте обычного cancel и safety action фиксируется в safety model; этот документ задаёт только общую propagation и ownership semantics.

## Visibility

- Root instance видим его authority через ACK, query snapshot и operation events, если transport profile предоставляет соответствующий канал.
- Child instance видим parent operation и может быть раскрыт внешнему клиенту как диагностическая часть root snapshot/event. Child не является независимо управляемым root для внешнего клиента.
- Primitive не имеет внешней identity и не появляется в operation ACK или lifecycle query.
- Query snapshot является authoritative read-моделью текущего состояния. Events/subscriptions являются bounded delivery mechanism и не заменяют reconciliation через query после потери доставки.
- После смены `controllerEpoch` snapshot обязан явно сообщать текущий epoch; instance identity другого epoch не считается текущей активной операцией.

## Поля, отложенные без потери расширяемости

Общий contract оставляет точки расширения в `parameters`, operation-type metadata и typed outcome context. До отдельного решения не вводятся:

- progress percentage или progress counters;
- `MaxIterations` и заранее известное число child instances;
- wire-specific timestamp/ordering fields;
- persistence/recovery semantics для active operations через reboot;
- численные budgets и точные retention/timeout значения.

Эти отложенные решения не могут менять уже утверждённые identity, admission-before-ACK, ownership, lifecycle и epoch/idempotency boundaries без impact review карты #1.
