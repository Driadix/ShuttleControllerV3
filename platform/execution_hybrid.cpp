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

// Force-stop channel (issue 10: isolated preemptible critical path).
// Single writer per context (issue #43: ISR writes only its own slot):
// - force_stop_isr() runs in bumper ISR context (target, priority 0) or is
//   called synchronously by the fault injector (host). It emits the min-ID
//   frame DIRECTLY through the registered handler - true preemption of any
//   running step (T_fs = T_isr + T_mailbox, C4; obligation #3/#13). The
//   handler must be ISR-safe: bxCAN mailbox write is register-level.
// - Repeated edges while pending collapse into one emission (Q7.2).
volatile bool g_force_stop_pending = false;
std::uint32_t g_force_stop_count = 0;
// T_fs delta (latch -> emission), computed in ISR context into a single
// 32-bit slot: atomic on Cortex-M4, no torn 64-bit read (obligation #3).
volatile std::uint32_t g_last_force_stop_latency_us = 0;

// CAN emitter registered by the harness (invoked from ISR context).
StepFn g_force_stop_handler = nullptr;
void* g_force_stop_handler_ctx = nullptr;

std::uint64_t g_max_gap_ms = 0;
std::uint64_t g_idle_ticks = 0;
std::uint64_t g_last_tick_ms = 0;
bool g_initialized = false;

} // namespace

// Called from the bumper ISR (target, priority 0) or the fault injector (host).
// Preempts any running step: the min-ID force-stop frame is emitted here,
// synchronously, before the ISR returns (Q7.2: repeated edges collapse).
void force_stop_isr()
{
    if (!g_force_stop_pending)
    {
        g_force_stop_pending = true;
        const std::uint64_t latched = monotonic::ticks_us();
        ++g_force_stop_count;
        if (g_force_stop_handler != nullptr)
        {
            g_force_stop_handler(g_force_stop_handler_ctx); // ISR-safe emission
        }
        g_last_force_stop_latency_us = static_cast<std::uint32_t>(monotonic::ticks_us() - latched);
        g_force_stop_pending = false; // collapsed; next edge emits again
    }
    // While pending (already emitting), repeated edges collapse (Q7.2).
}

void register_force_stop_handler(StepFn fn, void* ctx)
{
    g_force_stop_handler = fn;
    g_force_stop_handler_ctx = ctx;
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
    // Clamp backward time jumps (issue #48 section 9): zero gap, never a
    // fabricated ~2^64 maximum.
    const std::uint64_t gap = now >= g_last_tick_ms ? now - g_last_tick_ms : 0;
    if (gap > g_max_gap_ms)
    {
        g_max_gap_ms = gap;
    }
    g_last_tick_ms = now;

    // The preemptible critical path is served synchronously inside
    // force_stop_isr() (ISR context); nothing to serve at the tick boundary.
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
std::uint64_t force_stop_latency_us() { return g_last_force_stop_latency_us; }
std::uint32_t force_stop_count() { return g_force_stop_count; }

void run()
{
    for (;;)
    {
        on_tick();
#ifdef __arm__
        const std::uint64_t deadline = monotonic::now_ms() + 1;
        while (monotonic::now_ms() < deadline)
        {
            __asm__ volatile("wfi"); // wait for the tick to advance
        }
#else
        // Host: tests drive on_tick() directly.
#endif
    }
}

} // namespace kernel
} // namespace slice

#endif // V3_KERNEL_HYBRID
