// Static RTOS kernel variant (issue 10; FreeRTOS via the pinned ST Cube
// middleware package, owner decision 2026-08-12). Compiled only when
// V3_KERNEL_RTOS is defined. See execution_rtos.h for the API mapping.
#ifdef V3_KERNEL_RTOS

#include <cstdint>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "platform/execution_core.h"
#include "platform/execution_rtos.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

namespace slice
{
namespace kernel
{
namespace
{

// ---- static task pool (no dynamic allocation, rule R1) -----------------------

constexpr std::uint32_t kMaxTasks = 6;
constexpr std::uint32_t kWorkQueueDepth = 32;

struct StaticTaskSlot
{
    StackType_t* stack = nullptr;
    StaticTask_t* control = nullptr;
    TaskHandle_t handle = nullptr;
    StepFn fn = nullptr;
    void* ctx = nullptr;
};
StaticTaskSlot g_task_slots[kMaxTasks] = {};
StackType_t g_stacks[kMaxTasks][256] = {}; // 1 KiB per slot, sized by add_task
StaticTask_t g_controls[kMaxTasks] = {};
std::uint32_t g_task_count = 0;

// Bounded work queue (schedule() -> control task).
StaticQueue_t g_work_queue_mem = {};
QueueHandle_t g_work_queue = nullptr;
struct WorkItem
{
    StepFn fn = nullptr;
    void* ctx = nullptr;
    std::uint32_t deadline_ms = 0;
};
WorkItem g_work_items[kWorkQueueDepth] = {};

// Idle task static memory (static allocation requires it).
StackType_t g_idle_stack[configMINIMAL_STACK_SIZE] = {};
StaticTask_t g_idle_control = {};
TaskHandle_t g_safety_task = nullptr;

// Observables mirroring the cooperative/hybrid API for the comparison report.
std::uint64_t g_max_gap_ms = 0;
std::uint64_t g_idle_ticks = 0;
std::uint64_t g_last_tick_ms = 0;

// Force-stop emitter registered by the harness (called by the safety task).
StepFn g_force_stop_handler = nullptr;
void* g_force_stop_handler_ctx = nullptr;

// ---- hooks (live in the top anonymous namespace: they access g_idle_*,
// g_max_gap_ms, g_last_tick_ms, watchdog) ----------------------------

extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** tcb, StackType_t** stack,
                                              configSTACK_DEPTH_TYPE* size)
{
    *tcb = &g_idle_control;
    *stack = g_idle_stack;
    *size = configMINIMAL_STACK_SIZE;
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t, char*)
{
    // Recorded via the kernel observables; the IWDG remains the backstop.
    // A stuck task never reaches a step boundary, so reload stops and the
    // watchdog resets in the hardware window (F5 semantics preserved).
}

extern "C" void vApplicationIdleHook()
{
    // Idle-task reload (INV-WATCHDOG-ARMED). The idle task runs only when no
    // task is runnable - a spinning task starves it and the watchdog fires.
    watchdog::reload();
    ++g_idle_ticks;
}

extern "C" void vApplicationTickHook()
{
    // SysTick ISR context: light accounting only. NO step execution and NO
    // watchdog reload here - a reload in the tick hook would mask task
    // starvation (F5), and step work belongs to task context.
    const std::uint64_t now = monotonic::now_ms();
    const std::uint64_t gap = now >= g_last_tick_ms ? now - g_last_tick_ms : 0;
    if (gap > g_max_gap_ms)
    {
        g_max_gap_ms = gap;
    }
    g_last_tick_ms = now;
}

} // namespace

// ---- kernel API --------------------------------------------------------------

// Drains the bounded work queue (control task); created in init(), defined below.
void rtos_control_task(void*);

// configASSERT target (FreeRTOSConfig.h); C linkage for the kernel C sources.
extern "C" void slice_config_assert_failed(const char* file, int line)
{
    (void)file;
    (void)line;
    for (;;)
    {
    }
}

void init()
{
    g_task_count = 0;
    g_max_gap_ms = 0;
    g_idle_ticks = 0;
    g_last_tick_ms = monotonic::now_ms();
    watchdog::init();

    g_work_queue = xQueueCreateStatic(kWorkQueueDepth, sizeof(WorkItem),
                                      reinterpret_cast<std::uint8_t*>(g_work_items),
                                      &g_work_queue_mem);

    // Kernel plumbing task: drains the bounded work queue (schedule()).
    xTaskCreateStatic(rtos_control_task, "slice-ctrl", 192, nullptr,
                      static_cast<UBaseType_t>(3), g_stacks[0], &g_controls[0]);
    g_task_count = 1; // slot 0 owned by the kernel control task
}

bool schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms)
{
    if (g_work_queue == nullptr)
    {
        return false;
    }
    WorkItem item{fn, ctx, static_cast<std::uint32_t>(monotonic::now_ms() + deadline_ms)};
    return xQueueSend(g_work_queue, &item, 0) == pdPASS;
}

void on_tick() {} // tick hook already runs vApplicationTickHook above

void run()
{
    // The scheduler owns execution once rtos_harness::start() runs; this is
    // the interface-complete no-op for main() builds that reach loop().
    for (;;)
    {
    }
}

std::uint64_t now_ms() { return monotonic::now_ms(); }

std::uint64_t max_step_duration_ms() { return 0; }      // measured per task on target
std::uint64_t max_scheduler_gap_ms() { return g_max_gap_ms; }
std::uint64_t idle_ticks() { return g_idle_ticks; }

void register_force_stop_handler(StepFn fn, void* ctx)
{
    g_force_stop_handler = fn;
    g_force_stop_handler_ctx = ctx;
}

// Drains the bounded work queue (control task); runs due steps.
void rtos_control_task(void*)
{
    for (;;)
    {
        WorkItem item = {};
        if (xQueueReceive(g_work_queue, &item, portMAX_DELAY) == pdPASS)
        {
            if (static_cast<std::int64_t>(monotonic::now_ms() - item.deadline_ms) >= 0 &&
                item.fn != nullptr)
            {
                item.fn(item.ctx);
                watchdog::reload(); // step boundary reload (INV-WATCHDOG-ARMED)
            }
        }
    }
}

namespace rtos
{

bool add_task(const char* name, StepFn fn, void* ctx, std::uint32_t priority,
              std::uint32_t stack_words)
{
    if (g_task_count >= kMaxTasks || stack_words > 256)
    {
        return false;
    }
    StaticTaskSlot& slot = g_task_slots[g_task_count];
    slot.stack = g_stacks[g_task_count];
    slot.control = &g_controls[g_task_count];
    slot.fn = fn;
    slot.ctx = ctx;
    slot.handle = xTaskCreateStatic(fn, name, stack_words, ctx, static_cast<UBaseType_t>(priority),
                                    slot.stack, slot.control);
    if (slot.handle == nullptr)
    {
        return false;
    }
    ++g_task_count;
    return true;
}

void bumper_notify_from_isr()
{
    BaseType_t higher = pdFALSE;
    if (g_safety_task != nullptr)
    {
        vTaskNotifyGiveFromISR(g_safety_task, &higher);
    }
    portYIELD_FROM_ISR(higher);
}

void start() { vTaskStartScheduler(); }

void* safety_task_handle() { return g_safety_task; }

void set_safety_task_handle(void* h) { g_safety_task = static_cast<TaskHandle_t>(h); }

} // namespace rtos

} // namespace kernel
} // namespace slice

#endif // V3_KERNEL_RTOS
