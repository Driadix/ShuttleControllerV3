// Shared bounded-step ring used by the cooperative and hybrid kernels.
// Internal to platform/; not part of the public kernel API.
#pragma once

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

namespace slice
{
namespace kernel
{
namespace coop
{

class StepRing
{
  public:
    // Runs all due steps to completion, recording overruns against StepBudgetMs.
    // Returns the number of steps executed.
    std::uint32_t run_due(std::uint64_t now)
    {
        std::uint32_t executed = 0;
        while (m_count > 0 && executed < MaxSteps)
        {
            Step& s = m_steps[m_head];
            if (static_cast<std::int64_t>(now - s.deadline_ms) < 0)
            {
                break; // not due yet
            }
            const std::uint64_t t0 = now_us();
            s.fn(s.ctx);
            const std::uint64_t dt_ms = (now_us() - t0) / 1000;
            if (dt_ms > m_max_step_ms)
            {
                m_max_step_ms = dt_ms;
            }
            if (dt_ms > StepBudgetMs)
            {
                watchdog::report_overrun(dt_ms);
            }
            m_head = (m_head + 1) % MaxSteps;
            --m_count;
            ++executed;
        }
        return executed;
    }

    bool schedule(StepFn fn, void* ctx, std::uint32_t deadline_ms, std::uint64_t now)
    {
        if (m_count >= MaxSteps)
        {
            return false; // observable overload (obligation #7)
        }
        const std::uint32_t slot = (m_head + m_count) % MaxSteps;
        m_steps[slot] = Step{fn, ctx, static_cast<std::uint32_t>(now + deadline_ms)};
        ++m_count;
        return true;
    }

    bool empty() const { return m_count == 0; }
    std::uint32_t count() const { return m_count; }
    std::uint64_t max_step_ms() const { return m_max_step_ms; }

  private:
    static std::uint64_t now_us() { return monotonic::ticks_us(); }

    Step m_steps[MaxSteps] = {};
    std::uint32_t m_head = 0;
    std::uint32_t m_count = 0;
    std::uint64_t m_max_step_ms = 0;
};

} // namespace coop
} // namespace kernel
} // namespace slice
