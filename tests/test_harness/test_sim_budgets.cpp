// Host-simulation budget checks (issue #54 host-only scope; design doc s14).
// The C1 chain and combined load are exercised with physically grounded
// CPU-costs (datasheet/code-derived, issue #48 s1): results are
// host-simulation, never measured. Budgets: T_step = 10 ms, T_eso <= 70 ms,
// T_fresh + T_eso <= 370 ms, watchdog fast-end 6.8 s.
#include <gtest/gtest.h>

#include <cstdint>

#include "domain/safety_health.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"
#include "proving/fakes.h"
#include "proving/faults.h"
#include "proving/harness.h"
#include "proving/loads.h"
#include "proving/sim.h"

namespace
{

using namespace slice;
using namespace slice::kernel;
using namespace slice::proving::sim;

using slice::proving::FakeCanPort;
using slice::proving::HarnessState;
using slice::proving::Measurement;

// C1 chain with modelled peripheral cost: sensing (blocking I2C ToF read) ->
// safety -> arbitration -> actuation (CAN TX). The virtual T_eso is the
// sum of the step CPU-costs and must stay inside the 70 ms analytical budget.
TEST(SimBudgets, C1ChainEsoInsideBudgetWithPhysicalCosts)
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

    // Stale fault, then drive the chain with physical CPU-costs.
    slice::proving::faults::sensor_stale(state);
    CpuClock clk;
    std::uint64_t eso_us = 0;
    for (std::uint64_t tick = 1; tick <= 320; ++tick) // exceed T_fresh (300 ms)
    {
        monotonic::test_set_time_ms(tick);
        clk.reset();
        i2c_tof_read(clk);              // sensing step: blocking ToF read
        slice::proving::schedule_standard_steps(state);
        kernel::on_tick();
        can_tx(clk, 1);                 // actuator emission (0.216 ms)
        if (state.stop_pending_trace == false &&
            state.arb.current().kind == slice::IntentKind::Stop && eso_us == 0)
        {
            // Stop intent was applied this tick; capture the virtual ESO.
            eso_us = clk.consumed_us();
        }
    }

    EXPECT_EQ(state.arb.current().kind, slice::IntentKind::Stop);
    // Virtual T_eso ~ 1.2 ms (I2C read) + 0.216 ms (CAN TX) << 70 ms budget.
    EXPECT_GT(eso_us, 0);
    EXPECT_LE(eso_us, 70'000);
    // Full chain: T_fresh (300 ms) + T_eso <= 370 ms analytical.
    EXPECT_LE(300'000 + eso_us, 370'000);
}

// Flash erase window: 4 s is the only allowed T_step exception (quiescent);
// the watchdog fast-end window (6.8 s) must still hold (no reset).
TEST(SimBudgets, FlashWindowStaysInsideWatchdogWindow)
{
    monotonic::init();
    kernel::init();

    CpuClock clk;
    flash_erase(clk);
    EXPECT_EQ(clk.consumed_us(), kFlashEraseUs);

    // The erase window (4 s) is less than the watchdog fast end (6.8 s,
    // issue #48 s3): no reload for 4 s must not starve the watchdog.
    monotonic::test_set_time_ms(1);
    kernel::on_tick(); // reload at the step boundary before the window
    monotonic::test_set_time_ms(1 + 4'000); // 4 s window elapses, no ticks
    EXPECT_FALSE(watchdog::starved());
}

// CAN flood: RX drain of > 64 frames/tick stays inside T_step; overflow is
// observable (obligation #7/#13).
TEST(SimBudgets, CanFloodDrainInsideStepBudget)
{
    monotonic::init();
    kernel::init();

    CpuClock clk;
    const std::uint64_t drain_us = can_rx_drain(clk, 80); // > 64 frames/tick
    EXPECT_LE(drain_us, kernel::StepBudgetMs * 1000);
    EXPECT_EQ(drain_us, 80 * kCanRxDrainUsPerFrame);

    FakeCanPort can;
    // More than the fake RX ring capacity (256): overflow observable.
    slice::proving::loads::rx_flood(can, 300);
    EXPECT_GT(can.rx_overflow(), 0); // observable overflow
}

// Combined load per tick: sensing (I2C) + CAN RX drain + UART drain + CAN TX
// + log emission must fit the 10 ms bounded-step budget.
TEST(SimBudgets, CombinedLoadFitsStepBudget)
{
    CpuClock clk;
    i2c_tof_read(clk);                 // 1.21 ms  (sensing)
    can_rx_drain(clk, 64);             // 0.128 ms (transport, budget max)
    uart_drain(clk, 230, 10);          // 0.23 ms  (display budget max)
    can_tx(clk, 16);                   // 0.016 ms (actuator budget max)
    EXPECT_LE(clk.consumed_us(), kernel::StepBudgetMs * 1000);
}

} // namespace
