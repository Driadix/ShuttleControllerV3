// Execution core public API (design docs/execution-foundation-design-v3.md
// section 5.2). Production form: namespace v3::kernel replaces slice::kernel;
// process_tick() is foreground-only (NOT an ISR hook - rule R2); observable
// hooks are removed from the API (events go through KernelEvents); no test
// hooks in production (test injections via a fake TimeSource).
//
// The kernel owns: bounded step scheduling, watchdog reload policy, the
// mandatory safety boundary (SafetySlot, outside the FIFO), startup
// reset-cause reporting. All work runs in the foreground (loop / process_tick);
// the only ISR in scope #70 is the TIM2 clock adapter (++tick + seqlock).
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{
namespace kernel
{

// A bounded run-to-completion step. `ctx` is an opaque context pointer
// (no std::function: rule R1 - no dynamic allocation).
using StepFn = void (*)(void* ctx);

struct Step
{
    StepFn fn = nullptr;
    void* ctx = nullptr;
    std::uint32_t deadline_ms = 0; // ABSOLUTE monotonic deadline (release time)
};

// Typed schedule outcome: no silent loss of information (rule R5).
enum class ScheduleResult : std::uint8_t
{
    Ok = 0,
    QueueFull = 1,          // step queue full (obs #7)
    DeadlineOutOfWindow = 2 // deadline outside [now, now + StepBudgetMs]
};

// Bounded step queue capacity (max steps per tick; synthetic max load L7).
constexpr std::uint32_t MaxSteps = 64;

// Bounded step budget (issue #48 section 4): every domain/adapter step <= 10 ms.
constexpr std::uint32_t StepBudgetMs = 10;

struct KernelConfig
{
    TimeSource* time = nullptr;       // domain/ports.h; required (adapter tim2_clock)
    WatchdogPort* hw = nullptr;       // domain/ports.h; required (adapter iwdg_watchdog)
    KernelEvents* events = nullptr;   // domain/ports.h; required (Phase 1: stub)
    ResetCauseSource* reset = nullptr; // domain/ports.h; required (adapter reset_cause)
    SafetySlot* safety = nullptr;     // domain/ports.h; required (Phase 1: stub)
};

// Startup (foreground): reset-cause -> events->reset_cause; monotonic::init;
// watchdog::init (IWDG armed before any flash operation, #49 section 8.2).
void init(const KernelConfig& cfg);

// Target foreground loop; never returns. WFI idle, one process_tick per tick.
void run();

// Foreground-only: processes at most one tick - at most one due FIFO step,
// the mandatory SafetySlot boundary (outside the FIFO), and an unconditional
// watchdog reload. NOT an ISR hook: never called from an ISR (rule R2).
void process_tick();

// Schedules a bounded step with an ABSOLUTE monotonic deadline. The deadline
// must be in [now, now + StepBudgetMs]; stale or out-of-window deadlines are
// rejected with DeadlineOutOfWindow (no silent success, rule R5). When the
// queue is full the schedule_rejected event is emitted and QueueFull returned.
ScheduleResult schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms);

} // namespace kernel
} // namespace v3
