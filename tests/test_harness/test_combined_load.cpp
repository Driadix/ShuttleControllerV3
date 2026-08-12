// Combined-load boundedness (issue 10 mandatory loads; obligation #8: every
// step <= T_step under combined load; #7: overflows observable; #12: log storm
// never blocks a step).
#include <gtest/gtest.h>

#include <cstdint>

#include "domain/queues.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "proving/fakes.h"
#include "proving/loads.h"

namespace
{

using namespace slice;
using namespace slice::proving::loads;
using namespace slice::proving;

TEST(CombinedLoad, BoundedStepsHoldUnderFloodAndStorm)
{
    monotonic::init();
    kernel::init();

    FakeCanPort can;
    slice::QueueClasses queues;

    for (std::uint64_t tick = 1; tick <= 200; ++tick)
    {
        monotonic::test_set_time_ms(tick);

        // L7: maximum synthetic operation steps.
        loads::schedule_operation_steps(kernel::MaxSteps);
        // L4: log storm at max emission.
        loads::log_storm(queues, 40);
        // L1: CAN flood on both TX and RX paths.
        CanPort::Frame f = {};
        f.id = 0x2405;
        f.len = 8;
        can.inject_rx(f);
        can.inject_rx(f);
        can.inject_rx(f);
        loads::tx_backpressure(can, 4);

        kernel::on_tick();
    }

    // Every step stayed within the 10 ms budget (host: microseconds).
    EXPECT_LE(kernel::max_step_duration_ms(), kernel::StepBudgetMs);
    // Overload is observable, not silent (obligation #7).
    EXPECT_GT(queues.dropped_logs(), 0);
    EXPECT_GT(can.tx_dropped(), 0);
}

} // namespace
