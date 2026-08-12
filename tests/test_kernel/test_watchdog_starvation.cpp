// Watchdog starvation (fault F5, issue 10): a stuck foreground (no ticks
// driven) must be detected against the hardware window (host model: 6.8 s fast
// LSI end, issue #48 section 3). Reload at every step boundary and idle tick
// keeps the window satisfied.
#include <gtest/gtest.h>

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

namespace
{

using namespace slice;

TEST(WatchdogStarvation, MissedReloadsStarveWithinHardwareWindow)
{
    monotonic::init();
    kernel::init();

    // Steady ticks: never starved.
    for (std::uint64_t now = 1; now <= 1000; ++now)
    {
        monotonic::test_set_time_ms(now);
        kernel::on_tick();
        EXPECT_FALSE(watchdog::starved());
    }

    // Foreground stalls: no ticks driven, no reload. The fast-end window
    // (6.8 s) must expire.
    monotonic::test_set_time_ms(1000 + 6'800 - 1);
    EXPECT_FALSE(watchdog::starved());
    monotonic::test_set_time_ms(1000 + 6'800);
    EXPECT_TRUE(watchdog::starved());
}

TEST(WatchdogStarvation, ReloadAfterStallRecovers)
{
    monotonic::init();
    kernel::init();

    monotonic::test_set_time_ms(10'000);
    EXPECT_TRUE(watchdog::starved());

    kernel::on_tick(); // tick drives reload at step boundary / idle
    EXPECT_FALSE(watchdog::starved());
}

} // namespace
