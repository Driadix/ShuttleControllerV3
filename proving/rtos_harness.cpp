// RTOS task-mode harness driver. Compiled only for V3_KERNEL_RTOS (the ST
// Cube FreeRTOS package is ststm32-scoped, so this file is excluded from
// native host builds via lib_deps platform scope).
#ifdef V3_KERNEL_RTOS

#include "proving/rtos_harness.h"

#include <cstdint>

#include "FreeRTOS.h"
#include "task.h"

#include "domain/intent.h"
#include "platform/execution_core.h"
#include "platform/execution_rtos.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"
#include "proving/harness.h"
#include "proving/measurement.h"

namespace slice
{
namespace proving
{
namespace rtos_harness
{
namespace
{

HarnessState* g_state = nullptr;

void safety_task_body(void*)
{
    kernel::rtos::set_safety_task_handle(xTaskGetCurrentTaskHandle());
    for (;;)
    {
        // Bumper notification (from ISR): apply the ForceStop intent through
        // the funnel. RTOS semantics: the notification wakes the highest
        // priority task (preempts a running lower-priority task); the
        // actuator task emits the min-ID frame. Repeated edges collapse.
        const bool bumper = ulTaskNotifyTake(pdTRUE, 0) > 0;
        if (bumper && g_state->bumper_pending)
        {
            Intent fs{};
            fs.kind = IntentKind::ForceStop;
            fs.source = IntentSource::Safety;
            fs.seq = static_cast<std::uint32_t>(kernel::now_ms());
            g_state->arb.apply(fs);
            g_state->bumper_pending = false;
            if (g_state->measurement != nullptr)
            {
                g_state->stop_intent_at_us = monotonic::ticks_us(); // trigger
                g_state->stop_pending_trace = true;
            }
        }

        safety_step(g_state);
        watchdog::reload(); // step boundary reload (INV-WATCHDOG-ARMED)
        vTaskDelay(1);      // 1 ms tick
    }
}

void sensing_task_body(void*)
{
    for (;;)
    {
        sensing_step(g_state);
        vTaskDelay(1);
    }
}

void actuator_task_body(void*)
{
    for (;;)
    {
        actuator_step(g_state);
        vTaskDelay(1);
    }
}

void observability_task_body(void*)
{
    for (;;)
    {
        // Egress drain: queues are drained here; drops already counted by the
        // sink at push time (obligation #7). The 10 ms cadence bounds the
        // drain work per tick.
        vTaskDelay(10);
    }
}

} // namespace

void start(HarnessState& state)
{
    g_state = &state;

    const bool ok = kernel::rtos::add_task("slice-safety", safety_task_body, g_state, 5, 192) &&
                    kernel::rtos::add_task("slice-sensing", sensing_task_body, g_state, 4, 192) &&
                    kernel::rtos::add_task("slice-actuator", actuator_task_body, g_state, 2, 192) &&
                    kernel::rtos::add_task("slice-obs", observability_task_body, g_state, 1, 128);
    if (!ok)
    {
        for (;;)
        {
        } // task pool exhausted: configuration error, watchdog backstop
    }

    kernel::rtos::start(); // never returns on success
}

} // namespace rtos_harness
} // namespace proving
} // namespace slice

#endif // V3_KERNEL_RTOS
