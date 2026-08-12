// Cooperative kernel: bounded run-to-completion steps, one scheduling domain,
// one main stack (issue 10; budgets #48 section 4).
//
// Host leg: the tick is injected by tests (deterministic), time comes from the
// host clock implementation in monotonic.cpp. Target leg: TIM2 1 ms tick ISR.
#include <cstdint>

#include "platform/coop_core.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

#if defined(V3_KERNEL_COOP) && !defined(V3_KERNEL_HYBRID) && !defined(V3_KERNEL_RTOS)

namespace slice
{
namespace kernel
{
namespace
{

coop::StepRing g_ring;

std::uint64_t g_max_gap_ms = 0;
std::uint64_t g_idle_ticks = 0;
std::uint64_t g_last_tick_ms = 0;
bool g_initialized = false;

} // namespace

void init()
{
    g_ring = coop::StepRing{};
    g_max_gap_ms = 0;
    g_idle_ticks = 0;
    g_last_tick_ms = monotonic::now_ms();
    watchdog::init();
    g_initialized = true;
}

void on_tick()
{
    if (!g_initialized)
    {
        return;
    }
    const std::uint64_t now = monotonic::now_ms();
    // Clamp backward time jumps (NTP-style corrections, issue #48 section 9):
    // a backward jump is zero gap, never a fabricated ~2^64 maximum.
    const std::uint64_t gap = now >= g_last_tick_ms ? now - g_last_tick_ms : 0;
    if (gap > g_max_gap_ms)
    {
        g_max_gap_ms = gap;
    }
    g_last_tick_ms = now;

    if (g_ring.empty())
    {
        ++g_idle_ticks;
        watchdog::reload(); // idle-loop reload (issue #43 section 4)
        return;
    }
    g_ring.run_due(now);
    watchdog::reload(); // reload at every step boundary (INV-WATCHDOG-ARMED)
}

bool schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms)
{
    return g_ring.schedule(fn, ctx, deadline_ms, monotonic::now_ms());
}

// Cooperative variant: the funnel carries the ForceStop intent; no preemptible
// channel (issue #43 INV-FORCE-STOP-CHANNEL served at the next step boundary).
void register_force_stop_handler(StepFn, void*) {}

std::uint64_t now_ms() { return monotonic::now_ms(); }

std::uint64_t max_step_duration_ms() { return g_ring.max_step_ms(); }
std::uint64_t max_scheduler_gap_ms() { return g_max_gap_ms; }
std::uint64_t idle_ticks() { return g_idle_ticks; }

void run()
{
    // Target entry: process each monotonic tick exactly once. The first call
    // waits for tick 1 (initialization completes before any processing);
    // subsequent iterations wait until the clock strictly advances, so a step
    // overrun or the flash window (now jumps by > 1) becomes an explicit
    // catch-up: on_tick() processes all due steps at the new time and the
    // scheduler-gap observable records the jump (issue #48 section 4: the
    // flash window is the only allowed exception to T_step).
    std::uint64_t last_processed = monotonic::now_ms();
    for (;;)
    {
#ifdef __arm__
        while (monotonic::now_ms() <= last_processed)
        {
            __asm__ volatile("wfi"); // wake on any interrupt; re-check the tick
        }
        last_processed = monotonic::now_ms();
        on_tick();
#else
        // Host: tests drive on_tick() directly; run() is target-only.
        (void)last_processed;
#endif
    }
}

} // namespace kernel
} // namespace slice

#endif // V3_KERNEL_COOP
