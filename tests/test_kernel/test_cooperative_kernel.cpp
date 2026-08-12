// Cooperative kernel: bounded steps, scheduler gap, idle accounting, overload
// observability (issue 10; budgets #48 section 4: T_step = 10 ms).
// Host leg is deterministic: tests drive the injected clock and on_tick().
#include <gtest/gtest.h>

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/watchdog_policy.h"

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

void due_at(std::uint64_t now)
{
    // Deliver a tick at the requested virtual time.
    monotonic::test_set_time_ms(now);
    kernel::on_tick();
}
} // namespace

class KernelTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        monotonic::init();
        kernel::init();
        g_run_count = 0;
    }
};

TEST_F(KernelTest, StepRunsWhenDue)
{
    ASSERT_TRUE(kernel::schedule(count_step, nullptr, 1));
    due_at(1);
    EXPECT_EQ(g_run_count, 1);
}

TEST_F(KernelTest, StepDoesNotRunBeforeDeadline)
{
    ASSERT_TRUE(kernel::schedule(count_step, nullptr, 10));
    due_at(5);
    EXPECT_EQ(g_run_count, 0); // not due yet
    due_at(10);
    EXPECT_EQ(g_run_count, 1);
}

TEST_F(KernelTest, FullStepQueueIsObservableOverload)
{
    std::uint32_t scheduled = 0;
    while (kernel::schedule(count_step, nullptr, 1))
    {
        ++scheduled;
    }
    EXPECT_EQ(scheduled, kernel::MaxSteps);
    EXPECT_FALSE(kernel::schedule(count_step, nullptr, 1)); // overload observable
}

TEST_F(KernelTest, WatchdogReloadedOnStepBoundaryAndIdle)
{
    kernel::schedule(count_step, nullptr, 1);
    due_at(1);
    // Idle ticks reload too (INV-WATCHDOG-ARMED).
    const std::uint64_t last = watchdog::last_reload_ms();
    due_at(2);
    EXPECT_GT(watchdog::last_reload_ms(), last);
}

TEST_F(KernelTest, IdleTicksCountedForCpuMargin)
{
    due_at(1);
    due_at(2);
    due_at(3);
    EXPECT_GE(kernel::idle_ticks(), 3);
}

TEST_F(KernelTest, StepDurationObservedUnderBudget)
{
    kernel::schedule(count_step, nullptr, 1);
    due_at(1);
    // On host a trivial step is microseconds; the budget is 10 ms (issue #48).
    EXPECT_LE(kernel::max_step_duration_ms(), kernel::StepBudgetMs);
}

} // namespace
