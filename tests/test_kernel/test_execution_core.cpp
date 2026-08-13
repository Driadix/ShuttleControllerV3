// test_kernel suite: bounded steps, FIFO order, one-step-per-tick dispatch,
// queue capacity, deadline window, idle reload, scheduler gap, startup
// reset-cause, ISR boundary (design docs/execution-foundation-design-v3.md
// section 7.3 T1-T7, T14, T15, T17). Host, deterministic.
#include <fstream>
#include <sstream>
#include <gtest/gtest.h>

#include "platform/execution_core.h"
#include "tests/common/kernel_env.h"

namespace
{

// Steps record their execution order into a shared counter/array.
std::uint32_t g_executed_count = 0;
std::uint32_t g_executed_order[8] = {};
testfakes::KernelEnv* g_env = nullptr;

void record_step(void* ctx)
{
    const std::uint32_t id = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ctx));
    ASSERT_LT(g_executed_count, 8u);
    g_executed_order[g_executed_count++] = id;
}

// Step that consumes > T_step of CPU (advances ticks_us only, T1).
void step_overrun_ticks(void* ctx)
{
    static_cast<testfakes::TestTimeSource*>(ctx)->advance_us(11'000); // 11 ms > T_step
}

// Step that consumes exactly T_step (boundary: NOT an overrun).
void step_boundary_ticks(void* ctx)
{
    static_cast<testfakes::TestTimeSource*>(ctx)->advance_us(10'000); // 10 ms == T_step
}

// Step that advances the wall clock by ~T_step (T17 full-budget backlog).
void step_advance_wall(void* ctx)
{
    static_cast<testfakes::TestTimeSource*>(ctx)->advance_ms(10);
}

} // namespace

class KernelTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        g_executed_count = 0;
        g_env = &env;
        env.init();
    }

    void TearDown() override { g_env = nullptr; }

    testfakes::KernelEnv env;
};

// T1: each step <= T_step; overrun (> T_step) -> KernelEvents::step_overrun.
TEST_F(KernelTest, StepOverrunIsEmitted)
{
    const auto ok = v3::kernel::schedule(step_overrun_ticks, &env.time, 0);
    ASSERT_EQ(ok, v3::kernel::ScheduleResult::Ok);
    v3::kernel::process_tick();
    ASSERT_EQ(env.events.overrun_count(), 1u);
    EXPECT_EQ(env.events.overrun(0), 11u); // 11 ms measured
}

// T1 boundary: a step of exactly T_step is NOT an overrun.
TEST_F(KernelTest, StepAtBudgetBoundaryIsNotOverrun)
{
    ASSERT_EQ(v3::kernel::schedule(step_boundary_ticks, &env.time, 0),
              v3::kernel::ScheduleResult::Ok);
    v3::kernel::process_tick();
    EXPECT_EQ(env.events.overrun_count(), 0u);
}

// T2: FIFO order + not-before (deadline is release time, not priority); a
// blocked head (deadline in the future) holds the queue.
TEST_F(KernelTest, FifoOrderAndBlockingHead)
{
    // A inserted first with the LATER deadline: FIFO wins, A is the head.
    ASSERT_EQ(v3::kernel::schedule(record_step, reinterpret_cast<void*>(1), 10),
              v3::kernel::ScheduleResult::Ok);
    ASSERT_EQ(v3::kernel::schedule(record_step, reinterpret_cast<void*>(2), 5),
              v3::kernel::ScheduleResult::Ok);

    // At now=0 neither A (dl=10) nor B (dl=5) is due: queue waits.
    v3::kernel::process_tick();
    EXPECT_EQ(g_executed_count, 0u);

    // At now=5 B is due but A (head, dl=10) is not: blocking head holds it.
    env.time.advance_ms(5);
    v3::kernel::process_tick();
    EXPECT_EQ(g_executed_count, 0u);

    // At now=10 A runs, then B (FIFO order).
    env.time.advance_ms(5);
    v3::kernel::process_tick();
    env.time.advance_ms(5);
    v3::kernel::process_tick();
    ASSERT_EQ(g_executed_count, 2u);
    EXPECT_EQ(g_executed_order[0], 1u);
    EXPECT_EQ(g_executed_order[1], 2u);
}

// T3: dispatch contract - exactly ONE step per tick, N due steps -> N ticks.
TEST_F(KernelTest, OneStepPerTick)
{
    constexpr std::uint32_t kSteps = 5;
    for (std::uint32_t i = 0; i < kSteps; ++i)
    {
        ASSERT_EQ(v3::kernel::schedule(record_step, reinterpret_cast<void*>(static_cast<std::uintptr_t>(i + 1)), 0),
                  v3::kernel::ScheduleResult::Ok);
    }
    for (std::uint32_t tick = 0; tick < kSteps; ++tick)
    {
        const std::uint32_t before = g_executed_count;
        v3::kernel::process_tick();
        EXPECT_EQ(g_executed_count, before + 1) << "tick " << tick;
    }
    EXPECT_EQ(g_executed_count, kSteps);
}

// T4: MaxSteps=64; the 65th schedule -> QueueFull + schedule_rejected.
TEST_F(KernelTest, QueueCapacityAndRejectedEvent)
{
    for (std::uint32_t i = 0; i < v3::kernel::MaxSteps; ++i)
    {
        ASSERT_EQ(v3::kernel::schedule(record_step, nullptr, 0), v3::kernel::ScheduleResult::Ok);
    }
    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, 0), v3::kernel::ScheduleResult::QueueFull);
    EXPECT_EQ(env.events.rejected(), 1u);
}

// T5: deadline window - stale and out-of-window rejected; boundaries + uint32
// wrap valid.
TEST_F(KernelTest, DeadlineWindow)
{
    env.time.set_time_ms(1'000);

    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, 1'010), v3::kernel::ScheduleResult::Ok);   // now + T_step
    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, 1'011), v3::kernel::ScheduleResult::DeadlineOutOfWindow); // + T_step + 1
    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, 999), v3::kernel::ScheduleResult::DeadlineOutOfWindow);   // stale
    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, 1'000), v3::kernel::ScheduleResult::Ok);   // deadline == now
}

// T5 wrap: uint32 wrap near 2^32 must not break the modular window check.
TEST_F(KernelTest, DeadlineWindowWrap)
{
    constexpr std::uint64_t kNearWrap = 0xFFFFFFF0ull; // now just below 2^32
    env.time.set_time_ms(kNearWrap);

    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, static_cast<std::uint32_t>(kNearWrap + 10)),
              v3::kernel::ScheduleResult::Ok); // forward within window
    EXPECT_EQ(v3::kernel::schedule(record_step, nullptr, static_cast<std::uint32_t>(kNearWrap - 1)),
              v3::kernel::ScheduleResult::DeadlineOutOfWindow); // stale across wrap
}

// T6: idle reload - empty queue still reloads the watchdog every tick.
TEST_F(KernelTest, IdleReloadEveryTick)
{
    env.time.advance_ms(1);
    v3::kernel::process_tick();
    env.time.advance_ms(1);
    v3::kernel::process_tick();
    env.time.advance_ms(1);
    v3::kernel::process_tick();
    EXPECT_EQ(env.hw.reload_count(), 3u);
}

// T7: scheduler_gap only when process_tick entry is delayed > 3xT_step (30 ms);
// the legal step+slot combination (~21 ms) is NOT an event.
TEST_F(KernelTest, SchedulerGapThreshold)
{
    env.time.advance_ms(31); // > 30 ms
    v3::kernel::process_tick();
    ASSERT_EQ(env.events.gap_count(), 1u);
    EXPECT_EQ(env.events.gap(0), 31u);

    env.time.advance_ms(21); // legal step+slot combination
    v3::kernel::process_tick();
    EXPECT_EQ(env.events.gap_count(), 1u); // unchanged

    env.time.advance_ms(30); // exactly the threshold: NOT > 30
    v3::kernel::process_tick();
    EXPECT_EQ(env.events.gap_count(), 1u);
}

// T14: startup - reset cause read once and reported as the first event.
TEST_F(KernelTest, StartupResetCauseFirstEvent)
{
    testfakes::KernelEnv env2;
    env2.init(v3::ResetCause::Watchdog);
    ASSERT_EQ(env2.events.cause_count(), 1u);
    EXPECT_EQ(env2.events.cause(0), v3::ResetCause::Watchdog);
    EXPECT_EQ(env2.reset.read_count(), 1u);
}

// T17: INV-SENSING-FRESH under worst-case backlog - 64 full-budget due steps;
// the safety slot runs between every step; max interval <= step + slot
// (~21 ms), independent of queue size.
TEST_F(KernelTest, SafetyBoundaryUnderFullBacklog)
{
    for (std::uint32_t i = 0; i < v3::kernel::MaxSteps; ++i)
    {
        ASSERT_EQ(v3::kernel::schedule(step_advance_wall, &env.time, 0),
                  v3::kernel::ScheduleResult::Ok);
    }
    for (std::uint32_t tick = 0; tick < v3::kernel::MaxSteps; ++tick)
    {
        v3::kernel::process_tick();
    }
    EXPECT_EQ(env.safety.tick_count(), v3::kernel::MaxSteps); // safety runs every tick
    // Interval between consecutive safety ticks <= step + slot (~21 ms).
    EXPECT_LE(env.safety.max_interval_ms(), 21u);
    // No false scheduler gaps: each tick gap is one full-budget step (10 ms).
    EXPECT_EQ(env.events.gap_count(), 0u);
    // Steps at exactly T_step are not overruns.
    EXPECT_EQ(env.events.overrun_count(), 0u);
}

// T15 (host leg): ISR boundary - adapters/tim2_clock must not reference the
// kernel, watchdog policy or events (rule R2; include-lint check on the
// adapter TU). The nm-level check runs on the target build output.
TEST_F(KernelTest, IsrBoundaryIncludeLint)
{
    std::ifstream in("adapters/tim2_clock.cpp");
    ASSERT_TRUE(in.is_open()) << "adapters/tim2_clock.cpp not readable from test cwd";
    std::ostringstream src;
    src << in.rdbuf();
    const std::string code = src.str();

    EXPECT_EQ(code.find("platform/execution_core.h"), std::string::npos);
    EXPECT_EQ(code.find("platform/watchdog_policy.h"), std::string::npos);
    EXPECT_EQ(code.find("platform/monotonic.h"), std::string::npos);
    EXPECT_EQ(code.find("kernel::"), std::string::npos);
    EXPECT_EQ(code.find("watchdog::"), std::string::npos);
    EXPECT_EQ(code.find("events->"), std::string::npos);
    // The ISR only advances the clock: seqlock publish present, no reload.
    EXPECT_NE(code.find("++g_now_ms"), std::string::npos);
}
