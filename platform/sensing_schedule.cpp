// Composition-root glue implementation (see sensing_schedule.h). The kernel
// runs at most one bounded step per tick; this glue keeps the sensing slot
// alive across long steps and schedule rejections (review MAJOR fix, #63).
#include "platform/sensing_schedule.h"

#include "domain/sensing.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace v3
{
namespace sensing
{

void schedule_tick(void* ctx)
{
    auto* svc = static_cast<SensingService*>(ctx);

    const std::uint64_t now = monotonic::now_ms();
    svc->step(now);

    // Fresh deadline from a post-step clock read: a step that overran the
    // slot (stuck bus blocking the I2C transaction) must still re-arm with
    // a deadline inside the kernel window [now, now + T_step].
    const std::uint64_t now2 = monotonic::now_ms();
    const std::uint32_t deadline =
        static_cast<std::uint32_t>(now2) + svc->next_step_ms();
    if (kernel::schedule(&schedule_tick, ctx, deadline) != kernel::ScheduleResult::Ok)
    {
        // Queue full (kernel emitted schedule_rejected) or deadline out of
        // window: re-arm with a fresh in-window deadline. The acquisition
        // must not die silently - recovery depends on future slots.
        (void)kernel::schedule(&schedule_tick, ctx,
                               static_cast<std::uint32_t>(now2) + 1u);
    }
}

} // namespace sensing
} // namespace v3
