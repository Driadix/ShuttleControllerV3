#include "platform/watchdog_policy.h"

#include <cstdint>

#include "platform/monotonic.h"

#ifdef ARDUINO

// Target leg: STM32duino IWDG (IWatchdog). Nominal 10 s window at 32 kHz LSI;
// hardware range 6.8-18.8 s across LSI tolerance (issue #48 section 3).
// Reload is called by the execution core; a missed reload for longer than the
// fast-end window (6.8 s) causes a hardware reset.
#include "IWatchdog.h"

namespace slice
{
namespace watchdog
{
namespace
{

std::uint64_t g_max_stall_ms = 0;
std::uint64_t g_last_reload_ms = 0;
bool g_armed = false;

} // namespace

void init()
{
    IWatchdog.begin(10'000'000); // 10 s nominal (us units, issue #48 section 3)
    g_last_reload_ms = monotonic::now_ms();
    g_armed = true;
}

void reload()
{
    if (!g_armed)
    {
        return;
    }
    IWatchdog.reload();
    const std::uint64_t now = monotonic::now_ms();
    const std::uint64_t stall = now - g_last_reload_ms;
    if (stall > g_max_stall_ms)
    {
        g_max_stall_ms = stall;
    }
    g_last_reload_ms = now;
}

void report_overrun(std::uint64_t) {} // hardware IWDG is the backstop

std::uint64_t last_reload_ms() { return g_last_reload_ms; }
std::uint64_t max_stall_ms() { return g_max_stall_ms; }
bool starved() { return (monotonic::now_ms() - g_last_reload_ms) >= 6'800; }

} // namespace watchdog
} // namespace slice

#else

// Host leg: simulated watchdog against the injected virtual clock.
// Hardware window modelled on the fast LSI end: 6.8 s (issue #48 section 3).
namespace slice
{
namespace watchdog
{
namespace
{

std::uint64_t g_last_reload_ms = 0;
std::uint64_t g_max_stall_ms = 0;
bool g_armed = false;

constexpr std::uint64_t kWindowFastEndMs = 6'800; // 6.8 s @ 47 kHz LSI

} // namespace

void init()
{
    g_last_reload_ms = monotonic::now_ms();
    g_max_stall_ms = 0;
    g_armed = true;
}

void reload()
{
    if (!g_armed)
    {
        return;
    }
    const std::uint64_t now = monotonic::now_ms();
    const std::uint64_t stall = now - g_last_reload_ms;
    if (stall > g_max_stall_ms)
    {
        g_max_stall_ms = stall;
    }
    g_last_reload_ms = now;
}

void report_overrun(std::uint64_t step_ms)
{
    // An overrun delays the next reload; account it against the window.
    (void)step_ms;
}

std::uint64_t last_reload_ms() { return g_last_reload_ms; }
std::uint64_t max_stall_ms() { return g_max_stall_ms; }
bool starved() { return (monotonic::now_ms() - g_last_reload_ms) >= kWindowFastEndMs; }

} // namespace watchdog
} // namespace slice

#endif
