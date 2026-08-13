// test_watchdog suite: F5 starvation model (reload stopped -> starved in the
// 6.8-18.8 s window) and mandatory reload between consecutive flash windows
// (design docs/execution-foundation-design-v3.md section 7.3 T11, T12).
// Host, deterministic.
#include <gtest/gtest.h>

#include "platform/watchdog_policy.h"
#include "tests/common/kernel_env.h"

// T11 (F5): reload stopped -> starved in the fast-end window model (6.8 s).
// The kernel reloads on every tick; when ticks stop, the model window elapses.
TEST(WatchdogTest, StarvationWhenReloadStops)
{
    testfakes::KernelEnv env;
    env.init();

    // At 6.799 s since the last reload the window has NOT elapsed.
    env.time.advance_ms(6'799);
    EXPECT_FALSE(env.hw.starved());

    // Crossing the fast end (6.8 s) -> starved.
    env.time.advance_ms(1);
    EXPECT_TRUE(env.hw.starved());
}

// T11 control: an active reload keeps the window from elapsing.
TEST(WatchdogTest, ReloadKeepsWindowOpen)
{
    testfakes::KernelEnv env;
    env.init();

    for (std::uint32_t i = 0; i < 700; ++i)
    {
        env.time.advance_ms(10);
        v3::kernel::process_tick(); // reloads every tick
    }
    EXPECT_EQ(env.hw.reload_count(), 700u);
    EXPECT_FALSE(env.hw.starved());
}

// T12: reload between consecutive flash windows. Two windows back-to-back
// (~8 s) exceed the 6.8 s fast end, so the policy issues a reload before the
// second window when needed (issue #48 section 3).
TEST(WatchdogTest, ReloadBetweenFlashWindows)
{
    testfakes::KernelEnv env;
    env.init();

    // First flash window starts at t=0: (0 - 0) + W_flash(4.013 s) <= 6.8 s,
    // no reload needed.
    v3::watchdog::note_flash_window();
    EXPECT_EQ(env.hw.reload_count(), 0u);

    // First window takes ~4 s, then the second window starts: elapsed since
    // reload (~4 s) + W_flash > 6.8 s => the policy reloads now (foreground).
    env.time.advance_ms(4'013);
    v3::watchdog::note_flash_window();
    EXPECT_EQ(env.hw.reload_count(), 1u);
    EXPECT_FALSE(env.hw.starved());
}

// T12 control: a reload between the windows is sufficient - the second window
// does not force an extra reload (the reload already happened).
TEST(WatchdogTest, ReloadBetweenFlashWindowsSufficient)
{
    testfakes::KernelEnv env;
    env.init();

    v3::watchdog::note_flash_window(); // first window at t=0
    env.time.advance_ms(4'013);
    v3::kernel::process_tick(); // reload between the windows (tick boundary)
    const std::uint32_t after_reload = env.hw.reload_count();
    ASSERT_EQ(after_reload, 1u);

    v3::watchdog::note_flash_window(); // second window right after the reload
    EXPECT_EQ(env.hw.reload_count(), after_reload); // no extra reload needed
}
