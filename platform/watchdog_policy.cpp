// Watchdog policy implementation (design section 2.3). Single implementation
// for host and target: the hardware is behind WatchdogPort; the flash-window
// reload model is deterministic against monotonic::now_ms().
#include "platform/watchdog_policy.h"

#include "platform/monotonic.h"

namespace v3
{
namespace watchdog
{
namespace
{

// Fast LSI end of the IWDG window (issue #48 section 3): 6.8 s.
constexpr std::uint64_t kFastEndMs = 6'800;

// Flash persistence window (quiescence C6, issue #48 section 3): ~4.013 s.
constexpr std::uint64_t kFlashWindowMs = 4'013;

WatchdogPort* g_hw = nullptr;
std::uint64_t g_last_reload_ms = 0;

} // namespace

void init(WatchdogPort& hw)
{
    g_hw = &hw;
    g_hw->init(10'000'000); // IWDG 10 s nominal (us units)
    g_last_reload_ms = monotonic::now_ms();
}

void reload()
{
    if (g_hw == nullptr)
    {
        return;
    }
    g_hw->reload();
    g_last_reload_ms = monotonic::now_ms();
}

void note_flash_window()
{
    if (g_hw == nullptr)
    {
        return;
    }
    // A flash window (blocking, ~W_flash) plus the time already elapsed since
    // the last reload must stay inside the 6.8 s fast end. If not, reload now
    // (still foreground, before the blocking window) - this is the mandatory
    // reload between consecutive flash windows (two windows back-to-back are
    // ~8 s > 6.8 s, issue #48 section 3).
    const std::uint64_t now = monotonic::now_ms();
    if ((now - g_last_reload_ms) + kFlashWindowMs > kFastEndMs)
    {
        g_hw->reload();
        g_last_reload_ms = now;
    }
}

void report_overrun(std::uint32_t step_ms)
{
    // Target: hardware IWDG is the backstop; nothing to do here. Host: the
    // starvation model lives in the test WatchdogPort fake (test_watchdog).
    (void)step_ms;
}

} // namespace watchdog
} // namespace v3
