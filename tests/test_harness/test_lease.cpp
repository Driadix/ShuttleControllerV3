// F3 lease expiry -> bounded CONTROLLED stop (obligation #6, INV-LEASE-STOP:
// T_lease_stop = T_step + T_ramp; budgets #48 section 2).
#include <gtest/gtest.h>

#include <cstdint>

#include "domain/intent.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "proving/fakes.h"
#include "proving/faults.h"
#include "proving/harness.h"

namespace
{

using namespace slice;
using namespace slice::monotonic;
using namespace slice::kernel;

using slice::proving::FakeCanPort;
using slice::proving::HarnessState;
using slice::proving::Measurement;

class LeaseTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        monotonic::init();
        kernel::init();
        state = HarnessState{};
        state.can = &can;
        state.measurement = &measurement;
        state.health.set_ready();
        // Manual session: hold-to-run active, lease expires at t=1000.
        state.manual_held = true;
        state.lease_expires_at_ms = 1000;
    }

    void drive_ticks(std::uint64_t count)
    {
        for (std::uint64_t i = 0; i < count; ++i)
        {
            monotonic::test_set_time_ms(monotonic::now_ms() + 1);
            slice::proving::schedule_standard_steps(state);
            kernel::on_tick();
        }
    }

    FakeCanPort can;
    Measurement measurement;
    HarnessState state;
};

TEST_F(LeaseTest, ExpiryProducesControlledStopIntent)
{
    drive_ticks(2000); // cross the lease expiry
    EXPECT_EQ(state.arb.current().kind, IntentKind::Stop);
    EXPECT_EQ(state.arb.current().stop_profile, StopProfile::Controlled);
    EXPECT_FALSE(state.manual_held); // lease consumed
    EXPECT_GT(can.tx_count(), 0);    // stop frame emitted (safe output)
}

TEST_F(LeaseTest, LinkLossFaultExpiresLeaseImmediately)
{
    drive_ticks(10); // before the nominal expiry
    EXPECT_EQ(state.arb.current().kind, IntentKind::VelocitySetpoint);

    // F3: link loss - the lease expires now, not at t=1000.
    slice::proving::faults::link_loss(state);
    drive_ticks(1);
    EXPECT_EQ(state.arb.current().kind, IntentKind::Stop);
    EXPECT_EQ(state.arb.current().stop_profile, StopProfile::Controlled);
}

} // namespace
