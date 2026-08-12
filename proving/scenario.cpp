#include "proving/scenario.h"

#include <cstdint>

#include "domain/queues.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "proving/harness.h"
#include "proving/loads.h"
#include "proving/measurement.h"
#include "proving/workload.h"

namespace slice
{
namespace proving
{

void run_scenario(HarnessState& state, const Workload& wl, std::uint32_t ticks)
{
    if (state.measurement != nullptr)
    {
        state.measurement->begin_run(wl);
    }

    for (std::uint32_t tick = 0; tick < ticks; ++tick)
    {
        monotonic::test_advance_ms(1); // host: virtual clock; target: no-op

        // Active load generators from the workload matrix (issue 10, L1-L9).
        if (wl.can_flood && state.can != nullptr)
        {
            loads::rx_flood(*state.can, 80); // > 64 frames/tick budget
            loads::tx_flood(*state.can, 20); // > 16 frames/tick budget
        }
        if (wl.log_storm && state.queues != nullptr)
        {
            loads::log_storm(*state.queues, 40); // max log emission
        }
        if (wl.operation_steps_per_tick > 0)
        {
            loads::schedule_operation_steps(wl.operation_steps_per_tick);
        }
        if (wl.i2c_stuck)
        {
            // L5 injection: the I2C adapter reports Stuck; recovery is driven
            // by the adapter FSM (target leg; host leg covered in tests).
        }

        schedule_standard_steps(state);
        kernel::on_tick();
    }

    if (state.measurement != nullptr)
    {
        state.measurement->finalize();
    }
}

} // namespace proving
} // namespace slice
