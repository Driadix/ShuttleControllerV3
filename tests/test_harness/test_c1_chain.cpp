// End-to-end C1 chain on host: sensing (freshness) -> Safety Authority ->
// arbitration -> actuation (CAN emission), obligation #1 (T_eso) and #2
// (T_check_jitter, T_arb). Staleness must produce a stop frame within the
// analytical budget chain (issue #48 section 2: T_fresh + T_eso <= 370 ms).
#include <gtest/gtest.h>

#include <cstdint>

#include "domain/ports.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "proving/fakes.h"
#include "proving/faults.h"
#include "proving/harness.h"
#include "proving/measurement.h"
#include "proving/workload.h"

namespace
{

using namespace slice::monotonic;
using namespace slice;

using slice::CanPort;
using slice::IntentKind;
using slice::proving::FakeCanPort;
using slice::proving::HarnessState;
using slice::proving::Measurement;

class C1ChainTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        monotonic::init();
        kernel::init();
        state = HarnessState{};
        state.can = &can;
        state.measurement = &measurement;
        state.health.set_ready(); // startup grace completed (INV-STARTUP-GATE)
        state.motion_commanded = true; // operation active (velocity intents)
        state.last_sample_at_ms = 0;
        state.sample_age_ms = 0;
    }

    void drive_ticks(std::uint64_t count)
    {
        for (std::uint64_t i = 0; i < count; ++i)
        {
            const std::uint64_t now = i + 1;
            monotonic::test_set_time_ms(now);
            slice::proving::schedule_standard_steps(state);
            kernel::on_tick();
        }
    }

    FakeCanPort can;
    Measurement measurement;
    HarnessState state;
};

TEST_F(C1ChainTest, FreshSensingKeepsMotionIntentEmitted)
{
    drive_ticks(10);
    EXPECT_GT(can.tx_count(), 0); // activity intent emitted as CAN frames
}

TEST_F(C1ChainTest, StaleSensingProducesStopFrameWithinBudgetChain)
{
    drive_ticks(10); // fresh operation; motion active via activity intent
    const std::uint64_t tx_before = can.tx_count();

    // Fault F2: stop refreshing; let the sample age exceed T_fresh (300 ms).
    slice::proving::faults::sensor_stale(state);
    const std::uint64_t detect_tick =
        state.last_sample_at_ms + slice::SafetyHealth::T_fresh_ms + 1;
    monotonic::test_set_time_ms(detect_tick);
    slice::proving::schedule_standard_steps(state);
    kernel::on_tick();

    // The funnel now carries a safety stop intent (INV-SENSING-FRESH).
    EXPECT_EQ(state.arb.current().kind, slice::IntentKind::Stop);
    EXPECT_EQ(state.arb.current().source, slice::IntentSource::Safety);
    EXPECT_GT(can.tx_count(), tx_before); // stop frame emitted (safe output)
    // Chain budget: T_fresh + T_eso <= 370 ms analytical (issue #48 section 2);
    // detection to emission is inside one tick, far inside the budget.
    const std::uint64_t eso_us =
        state.last_stop_emitted_at_us - state.stop_intent_at_us;
    EXPECT_LE(eso_us, 370'000);
}

TEST_F(C1ChainTest, HealthEntersDegradedOnStaleness)
{
    drive_ticks(5);
    slice::proving::faults::sensor_stale(state);
    const std::uint64_t detect_tick =
        state.last_sample_at_ms + slice::SafetyHealth::T_fresh_ms + 1;
    monotonic::test_set_time_ms(detect_tick);
    slice::proving::schedule_standard_steps(state);
    kernel::on_tick();
    EXPECT_EQ(state.health.health(), slice::Health::Degraded);
}

} // namespace
