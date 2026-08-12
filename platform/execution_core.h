// Execution core API shared by the three kernel variants (issue #51 section 12:
// common source tree, variant chosen by -DV3_KERNEL). The kernel owns: bounded
// step scheduling, watchdog reload policy, monotonic time, idle accounting.
#pragma once

#include <cstdint>

namespace slice
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
    std::uint32_t deadline_ms = 0; // relative to now_ms() at schedule time
};

// Bounded step queue capacity (max steps per tick; synthetic max load L7).
constexpr std::uint32_t MaxSteps = 64;

// Bounded step budget (issue #48 section 4): every domain/adapter step <= 10 ms.
constexpr std::uint32_t StepBudgetMs = 10;

// ---- kernel lifecycle ------------------------------------------------------

void init();
void run(); // never returns; target entry point
void on_tick(); // 1 ms tick hook: target timer ISR / host tests

// ---- scheduling ------------------------------------------------------------

// Schedules a bounded step. Returns false when the step queue is full
// (observable overload, obligation #7).
bool schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms);

// Registers the force-stop CAN emitter. Hybrid variant: called from the
// preemptible serve path at on_tick() (target: ISR context). Cooperative
// variant: no-op (the funnel carries the ForceStop intent instead).
void register_force_stop_handler(StepFn fn, void* ctx);

// ---- time ------------------------------------------------------------------

// Monotonic time in ms since init; wrap-safe 64-bit (issue #48 section 9).
std::uint64_t now_ms();

// ---- instrumentation (harness) ----------------------------------------------

// Current step budget check: records an overrun when the step exceeded
// StepBudgetMs. Observables for the measurement recorder (obligation #8).
std::uint64_t max_step_duration_ms();
std::uint64_t max_scheduler_gap_ms(); // longest gap between step boundaries
std::uint64_t idle_ticks();           // ticks with zero work (CPU margin)

} // namespace kernel
} // namespace slice
