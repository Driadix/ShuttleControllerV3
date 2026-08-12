// Hybrid kernel: cooperative core + isolated preemptible critical path
// (issue 10: "изолированный critical execution path с cooperative/event-driven
// orchestration"). Slice-minimum: the cooperative step machinery from coop, plus
// a force-stop channel that bypasses the step queue.
//
// Target: bumper edge ISR (priority 0) writes the min extended-ID force-stop
// frame directly from ISR context - it preempts any running step (hardware
// preemption). Host: preemption is simulated - force_stop_isr() latches a flag
// that on_tick() processes before any due step; the shared arbitration logic is
// identical to the cooperative variant (fair comparison of execution semantics).
#include <cstdint>

#include "platform/coop_core.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

#if defined(V3_KERNEL_HYBRID) && !defined(V3_KERNEL_COOP) && !defined(V3_KERNEL_RTOS)

namespace slice
{
namespace kernel
{
namespace
{

coop::StepRing g_ring;

// Force-stop channel. ISR side writes; step side reads. Single writer per
// context (issue #43: ISR writes only its own bounded slot).
volatile bool g_force_stop_pending = false;
std::uint64_t g_force_stop_latched_us = 0;
std::uint64_t g_force_stop_served_us = 0;
std::uint32_t g_force_stop_count = 0;

std::uint64_t g_max_gap_ms = 0;
std::uint64_t g_idle_ticks = 0;
std::uint64_t g_last_tick_ms = 0;
bool g_initialized = false;

} // namespace

// Called from the bumper ISR (target, priority 0) or the fault injector (host).
void force_stop_isr()
{
    if (!g_force_stop_pending)
    {
        g_force_stop_latched_us = monotonic::ticks_us();
        ++g_force_stop_count;
    }
    g_force_stop_pending = true; // repeated edges collapse into one (Q7.2)
}

void init()
{
    g_ring = coop::StepRing{};
    g_force_stop_pending = false;
    g_force_stop_count = 0;
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
    const std::uint64_t gap = now - g_last_tick_ms;
    if (gap > g_max_gap_ms)
    {
        g_max_gap_ms = gap;
    }
    g_last_tick_ms = now;

    // Preemptible critical path first: force-stop is served before any step.
    if (g_force_stop_pending)
    {
        g_force_stop_pending = false;
        g_force_stop_served_us = monotonic::ticks_us();
        // The CAN adapter's force_stop_tx() is invoked by the harness at this
        // point (target: from ISR context via direct mailbox write).
    }

    if (g_ring.empty())
    {
        ++g_idle_ticks;
        watchdog::reload();
        return;
    }
    g_ring.run_due(now);
    watchdog::reload();
}

bool schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms)
{
    return g_ring.schedule(fn, ctx, deadline_ms, monotonic::now_ms());
}

std::uint64_t now_ms() { return monotonic::now_ms(); }

std::uint64_t max_step_duration_ms() { return g_ring.max_step_ms(); }
std::uint64_t max_scheduler_gap_ms() { return g_max_gap_ms; }
std::uint64_t idle_ticks() { return g_idle_ticks; }

// Force-stop observables (obligation #3/#13: T_fs = latch -> served).
std::uint64_t force_stop_latency_us() { return g_force_stop_served_us - g_force_stop_latched_us; }
std::uint32_t force_stop_count() { return g_force_stop_count; }

void run()
{
    for (;;)
    {
        on_tick();
#ifdef __arm__
        __asm__ volatile("wfi");
#else
        // Host: tests drive on_tick() directly.
#endif
    }
}

} // namespace kernel
} // namespace slice

#endif // V3_KERNEL_HYBRID
