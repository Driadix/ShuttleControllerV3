// Monotonic robustness (obligation #9): wrap-safe deadline arithmetic and
// immunity to time source jumps (issue #48 section 9: all domain timeouts
// count from monotonic; NTP-jump must not corrupt scheduler observables).
#include <gtest/gtest.h>

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace
{

using namespace slice;

namespace
{
std::uint32_t g_run_count = 0;

void count_step(void*)
{
    ++g_run_count;
}
} // namespace

TEST(MonotonicWrap, DeadlinesFireAfterUint64Wrap)
{
    monotonic::init();
    kernel::init();
    g_run_count = 0;

    // Place the clock just below the 64-bit wrap and schedule a step beyond it.
    const std::uint64_t near_wrap = ~0ull - 5;
    monotonic::test_set_time_ms(near_wrap);
    ASSERT_TRUE(kernel::schedule(count_step, nullptr, 10));

    monotonic::test_set_time_ms(near_wrap + 10); // crosses the wrap
    kernel::on_tick();
    EXPECT_EQ(g_run_count, 1); // wrap-safe signed deadline arithmetic
}

TEST(MonotonicBackwardJump, GapStaysBounded)
{
    monotonic::init();
    kernel::init();

    monotonic::test_set_time_ms(1000);
    kernel::on_tick();
    monotonic::test_set_time_ms(500); // backward jump (NTP-style correction)
    kernel::on_tick();

    // The scheduler gap must not underflow to ~2^64 (review finding m2):
    // a backward jump is clamped to zero gap, not a fabricated maximum.
    EXPECT_LE(kernel::max_scheduler_gap_ms(), 1000);
}

TEST(MonotonicForwardJump, GapReflectsOnlyRealElapsed)
{
    monotonic::init();
    kernel::init();

    monotonic::test_set_time_ms(1000);
    kernel::on_tick();
    monotonic::test_set_time_ms(2000); // forward jump (2 s)
    kernel::on_tick();
    EXPECT_EQ(kernel::max_scheduler_gap_ms(), 1000);
}

} // namespace
