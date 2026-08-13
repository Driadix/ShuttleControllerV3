// Bounded step ring (evolution of the slice coop::StepRing, design
// docs/execution-foundation-design-v3.md section 2.1). Pure container: no
// dependency on domain/ports.h (rule R6) - the kernel (owner of the ports)
// emits events and reports overruns. Executes at most ONE due step per call
// (dispatch contract: one bounded step per tick; reload after each step is the
// caller's responsibility, INV-WATCHDOG-ARMED).
#pragma once

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace v3
{
namespace kernel
{

class StepRing
{
  public:
    struct StepRunResult
    {
        bool executed = false;
        std::uint32_t step_ms = 0; // measured step duration (ms); 0 if not executed
    };

    // Executes at most one due step. Blocking head (deadline in the future) is
    // NOT executed; the queue waits (deadlines are bounded to the T_step
    // window by kernel::schedule, so the block is at most T_step).
    // Wrap-safe deadline comparison: int32(now_u32 - deadline) < 0 => not due
    // (valid while the deadline window <= 2^31 ms; ours is <= T_step).
    StepRunResult run_next(std::uint64_t now)
    {
        StepRunResult result;
        if (m_count == 0)
        {
            return result;
        }
        const Step& s = m_steps[m_head];
        const std::uint32_t now_u32 = static_cast<std::uint32_t>(now);
        if (static_cast<std::int32_t>(now_u32 - s.deadline_ms) < 0)
        {
            return result; // blocking head: not due yet (<= T_step)
        }
        const std::uint64_t t0 = monotonic::ticks_us();
        s.fn(s.ctx);
        result.step_ms = static_cast<std::uint32_t>((monotonic::ticks_us() - t0) / 1000);
        m_head = (m_head + 1) % MaxSteps;
        --m_count;
        result.executed = true;
        return result;
    }

    // Stores the ABSOLUTE deadline as given (kernel::schedule validated the
    // window [now, now + StepBudgetMs] before calling). Capacity-only check.
    ScheduleResult schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms)
    {
        if (m_count >= MaxSteps)
        {
            return ScheduleResult::QueueFull;
        }
        const std::uint32_t slot = (m_head + m_count) % MaxSteps;
        m_steps[slot] = Step{fn, ctx, deadline_ms};
        ++m_count;
        return ScheduleResult::Ok;
    }

    bool empty() const { return m_count == 0; }
    std::uint32_t count() const { return m_count; }

  private:
    Step m_steps[MaxSteps] = {};
    std::uint32_t m_head = 0;
    std::uint32_t m_count = 0;
};

} // namespace kernel
} // namespace v3
