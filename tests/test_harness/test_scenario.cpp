// Scenario runner (M1/M3 fix): populates the workload matrix, drives the
// standard steps + loads for N ticks, and produces the measurement record
// (observed maxima + worst trace with workload metadata, issue #52 section 6.3).
#include <gtest/gtest.h>

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "proving/fakes.h"
#include "proving/faults.h"
#include "proving/harness.h"
#include "proving/measurement.h"
#include "proving/scenario.h"
#include "proving/workload.h"

namespace
{

using namespace slice;
using namespace slice::monotonic;

using slice::proving::FakeCanPort;
using slice::proving::FakeObservabilitySink;
using slice::proving::HarnessState;
using slice::proving::Measurement;
using slice::proving::Workload;

TEST(Scenario, CombinedLoadRunRecordsMetricsAndTraces)
{
    monotonic::init();
    kernel::init();

    FakeCanPort can;
    FakeObservabilitySink sink;
    Measurement measurement;
    HarnessState state = HarnessState{};
    state.can = &can;
    state.queues = &sink.queues();
    state.measurement = &measurement;
    state.health.set_ready();
    state.motion_commanded = true;

    Workload wl = {};
    wl.scenario_id = "smoke-combined";
    wl.run_number = 1;
    wl.can_flood = true;
    wl.log_storm = true;
    wl.operation_steps_per_tick = 32;

    slice::proving::run_scenario(state, wl, 100);

    // Obligation #2 (chain sub-budgets) recorded on every tick; #1 (T_eso) is
    // only recorded on stop events, absent in a healthy run.
    EXPECT_GT(measurement.metric(2).count(), 0);
    EXPECT_EQ(measurement.metric(1).count(), 0);
    // CAN flood + log storm produced observable overload.
    EXPECT_GT(can.rx_overflow(), 0);
    EXPECT_GT(sink.queues().dropped_logs(), 0);
}

TEST(Scenario, StaleFaultProducesStopTraceWithinChainBudget)
{
    monotonic::init();
    kernel::init();

    FakeCanPort can;
    Measurement measurement;
    HarnessState state = HarnessState{};
    state.can = &can;
    state.measurement = &measurement;
    state.health.set_ready();
    state.motion_commanded = true;

    // Run fresh, then inject F2 (staleness) and keep running.
    Workload wl = {};
    wl.scenario_id = "smoke-stale";
    for (std::uint32_t i = 0; i < 10; ++i)
    {
        monotonic::test_advance_ms(1);
        slice::proving::schedule_standard_steps(state);
        kernel::on_tick();
    }
    slice::proving::faults::sensor_stale(state);
    for (std::uint32_t i = 0; i < 400; ++i) // exceed T_fresh (300 ms)
    {
        monotonic::test_advance_ms(1);
        slice::proving::schedule_standard_steps(state);
        kernel::on_tick();
    }

    EXPECT_EQ(state.arb.current().kind, slice::IntentKind::Stop);
    // T_fresh + T_eso <= 370 ms analytical chain (issue #48 section 2):
    // detection to emission happened inside one tick, far within budget.
    const std::uint64_t eso_us =
        state.last_stop_emitted_at_us - state.stop_intent_at_us;
    EXPECT_LE(eso_us, 370'000)
        << "last_stop_us=" << state.last_stop_emitted_at_us
        << " stop_intent_us=" << state.stop_intent_at_us;
}

} // namespace
