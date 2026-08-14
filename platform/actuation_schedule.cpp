// Composition-root glue implementation (see actuation_schedule.h). Bounded
// emission step: one run per kernel tick while armed, per-gate (50 ms) inside
// the ActuatorController. Re-arm deadline from a FRESH monotonic read after the
// step (long step must not stale the deadline - kernel rejects out-of-window;
// on rejection, re-arm with now+1 so emission never dies silently, pattern #63).
#include "platform/actuation_schedule.h"

#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace v3
{
namespace safety
{
namespace
{

constexpr std::uint32_t kEmissionStepMs = 10; // bounded step window (T_step)

ActuatorController* g_actuator = nullptr;
bool g_armed = false;

} // namespace

void actuation_bind(ActuatorController* ac)
{
    g_actuator = ac;
}

bool actuation_armed()
{
    return g_armed;
}

void actuation_arm(std::uint64_t now)
{
    if (g_armed)
    {
        return;
    }
    g_armed = true;
    (void)kernel::schedule(&actuation_step, nullptr,
                           static_cast<std::uint32_t>(now) + 1u);
}

void actuation_step(void*)
{
    if (g_actuator == nullptr)
    {
        g_armed = false;
        return;
    }
    const std::uint64_t now = monotonic::now_ms();
    g_actuator->step(now);
    const std::uint64_t now2 = monotonic::now_ms(); // свежий now (pattern #63)
    if (g_actuator->active())
    {
        const std::uint32_t deadline = static_cast<std::uint32_t>(now2) + kEmissionStepMs;
        if (kernel::schedule(&actuation_step, nullptr, deadline) != kernel::ScheduleResult::Ok)
        {
            (void)kernel::schedule(&actuation_step, nullptr,
                                   static_cast<std::uint32_t>(now2) + 1u);
        }
    }
    else
    {
        g_armed = false; // disarm: idle = тишина на шине (fail-safe приводов, Q7.1 A)
    }
}

} // namespace safety
} // namespace v3
