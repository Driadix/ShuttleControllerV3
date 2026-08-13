// Execution core implementation (design docs/execution-foundation-design-v3.md
// sections 3.1, 3.2, 6). Foreground-only policy: ISRs do NOT call into here
// (rule R2); the only ISR in scope #70 is the TIM2 clock adapter.
#include "platform/execution_core.h"

#include "platform/coop_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

namespace v3
{
namespace kernel
{
namespace
{

StepRing g_ring;
TimeSource* g_time = nullptr;
WatchdogPort* g_hw = nullptr;
KernelEvents* g_events = nullptr;
SafetySlot* g_safety = nullptr;
std::uint64_t g_last_tick_ms = 0;
bool g_initialized = false;

} // namespace

void init(const KernelConfig& cfg)
{
    // Startup order (issue #43 section 5): reset-cause first (crash record
    // through reboot, #49 section 13), then monotonic, then watchdog - IWDG
    // armed before any flash operation (#49 section 8.2).
    g_events = cfg.events;
    g_events->reset_cause(cfg.reset->read());

    monotonic::init(*cfg.time);
    watchdog::init(*cfg.hw);

    g_time = cfg.time;
    g_hw = cfg.hw;
    g_safety = cfg.safety;
    g_ring = StepRing{};
    g_last_tick_ms = monotonic::now_ms();
    g_initialized = true;
}

void process_tick()
{
    if (!g_initialized)
    {
        return;
    }
    const std::uint64_t now = monotonic::now_ms();
    // Clamp backward time jumps (NTP-style corrections, issue #48 section 9):
    // a backward jump is zero gap, never a fabricated ~2^64 maximum.
    const std::uint64_t gap = now >= g_last_tick_ms ? now - g_last_tick_ms : 0;
    if (gap > 3 * StepBudgetMs)
    {
        g_events->scheduler_gap(gap); // entry delayed > 3xT_step (30 ms)
    }
    g_last_tick_ms = now;

    // Dispatch contract (design section 2.1): at most ONE due FIFO step per
    // tick. A full 64-step backlog drains at <= 1 step/tick (bounded).
    if (!g_ring.empty())
    {
        const StepRing::StepRunResult r = g_ring.run_next(now);
        if (r.executed && r.step_ms > StepBudgetMs)
        {
            g_events->step_overrun(r.step_ms); // observed budget violation (obs #8)
            watchdog::report_overrun(r.step_ms);
        }
    }

    // Mandatory safety boundary, OUTSIDE the FIFO (design section 2.5):
    // freshness-check + arbitration on every step boundary. Between two
    // safety ticks at most one FIFO step (<= T_step) runs, so
    // T_check_jitter / T_arb <= 1 step regardless of queue backlog.
    const std::uint64_t t0 = monotonic::ticks_us();
    g_safety->tick(now);
    const std::uint32_t slot_ms = static_cast<std::uint32_t>((monotonic::ticks_us() - t0) / 1000);
    if (slot_ms > StepBudgetMs)
    {
        g_events->step_overrun(slot_ms); // safety slot content exceeded budget
        watchdog::report_overrun(slot_ms);
    }

    // Unconditional reload every tick: covers the step boundary, idle and a
    // blocked head (INV-WATCHDOG-ARMED, issue #43 section 4, #48 section 3).
    watchdog::reload();
}

ScheduleResult schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms)
{
    if (!g_initialized)
    {
        return ScheduleResult::DeadlineOutOfWindow;
    }
    // Absolute deadline, modular forward interval (wrap-safe, INV-MONOTONIC):
    // valid window [now, now + StepBudgetMs]; stale (deadline in the past) or
    // out-of-window => reject (rule R5: no silent success).
    const std::uint64_t now = monotonic::now_ms();
    const std::int32_t delta =
        static_cast<std::int32_t>(deadline_ms - static_cast<std::uint32_t>(now));
    if (delta < 0 || delta > static_cast<std::int32_t>(StepBudgetMs))
    {
        return ScheduleResult::DeadlineOutOfWindow;
    }
    if (g_ring.count() >= MaxSteps)
    {
        g_events->schedule_rejected(); // observable overload (obs #7)
        return ScheduleResult::QueueFull;
    }
    return g_ring.schedule(fn, ctx, deadline_ms);
}

void run()
{
    // Target entry: process each monotonic tick exactly once. WFI idle; wake
    // on any interrupt and re-check the tick. Host: tests drive process_tick()
    // directly (deterministic), run() is target-only.
    std::uint64_t last_processed = monotonic::now_ms();
    for (;;)
    {
#ifdef __arm__
        while (monotonic::now_ms() <= last_processed)
        {
            __asm__ volatile("wfi"); // wake on any interrupt; re-check the tick
        }
        last_processed = monotonic::now_ms();
        process_tick();
#else
        (void)last_processed; // host: never called
#endif
    }
}

} // namespace kernel
} // namespace v3
