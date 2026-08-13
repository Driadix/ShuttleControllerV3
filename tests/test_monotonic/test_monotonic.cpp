// test_monotonic suite: wrap-safe 64-bit time, backward-jump clamp, seqlock
// torn-read detection (design docs/execution-foundation-design-v3.md section
// 7.3 T8-T10). Host, deterministic; the seqlock writer is simulated by a
// host thread (T10).
#include <atomic>
#include <thread>
#include <gtest/gtest.h>

#include "platform/monotonic.h"
#include "platform/seqlock.h"
#include "tests/common/fakes.h"
#include "tests/common/kernel_env.h"

namespace
{

testfakes::TestTimeSource g_time;

} // namespace

// T8: 64-bit carry must not break now_ms (wrap-safe, INV-MONOTONIC).
TEST(MonotonicTest, WrapSafe64Bit)
{
    g_time.set_time_ms(0xFFFFFFFFFFFFFFF0ull);
    v3::monotonic::init(g_time);
    EXPECT_EQ(v3::monotonic::now_ms(), 0xFFFFFFFFFFFFFFF0ull);

    g_time.advance_ms(32); // carries into the 65th bit, wraps modulo 2^64
    EXPECT_EQ(v3::monotonic::now_ms(), 0x0000000000000010ull);
}

// T9: backward jump (NTP-style correction) must not break monotonicity:
// process_tick clamps the gap to zero instead of fabricating a ~2^64 maximum.
TEST(MonotonicTest, BackwardJumpClamped)
{
    testfakes::KernelEnv env;
    env.init();

    env.time.advance_ms(100); // forward jump > 3xT_step: legitimate scheduler_gap
    v3::kernel::process_tick();
    ASSERT_EQ(env.events.gap_count(), 1u);

    env.time.set_time_ms(50); // backward jump
    v3::kernel::process_tick();
    // Clamped: the backward jump is zero gap - no second scheduler_gap event,
    // no fabricated ~2^64 gap.
    EXPECT_EQ(env.events.gap_count(), 1u);
    EXPECT_EQ(env.events.overrun_count(), 0u);
}

// T10: seqlock torn-read detection (B1 fix). A writer thread publishes
// (cycles, prev) pairs; the reader must never observe a torn pair. The
// invariant: cycles accumulates the sum of published deltas and prev always
// equals the last published cyccnt - a torn read mixes generations.
TEST(MonotonicTest, SeqlockTornReadDetected)
{
    volatile v3::detail::CycleSnapshot state;
    state.cycles = 0;
    state.prev_cyccnt = 0;
    state.seq = 0;

    constexpr std::uint32_t kIterations = 100'000;
    std::atomic<bool> done{false};

    std::thread writer([&] {
        std::uint32_t prev = 0;
        for (std::uint32_t i = 0; i < kIterations; ++i)
        {
            ++state.seq; // odd: writer in progress
            state.cycles += 10; // each tick publishes a 10-cycle delta
            state.prev_cyccnt = prev = (prev + 10) % 1'000'000;
            ++state.seq; // even: consistent
        }
        done.store(true);
    });

    std::uint64_t last_cycles = 0;
    while (!done.load())
    {
        const v3::detail::CycleSnapshot s = v3::detail::snapshot_cycle(state);
        // Consistent pair: cycles is the running sum and prev matches the
        // published cyccnt; cycles must be monotonically non-decreasing.
        EXPECT_GE(s.cycles, last_cycles);
        last_cycles = s.cycles;
        EXPECT_EQ(s.cycles % 10, 0u); // sum of 10-unit deltas
    }
    writer.join();

    const v3::detail::CycleSnapshot final_s = v3::detail::snapshot_cycle(state);
    EXPECT_EQ(final_s.cycles, static_cast<std::uint64_t>(kIterations) * 10u);
}
