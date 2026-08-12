// Watchdog policy (issue #43 section 4, #48 section 3): reload owned by the
// execution core at every bounded step boundary and in the idle loop.
// Target: real IWDG, 10 s window (IWatchdog.begin(10000000), hardware range
// 6.8-18.8 s across LSI tolerance - budget reloads on the fast 6.8 s end).
// Host: simulated against the injected virtual clock (starvation test F5).
#pragma once

#include <cstdint>

namespace slice
{
namespace watchdog
{

void init();
void reload();
void report_overrun(std::uint64_t step_ms); // step overrun (obligation #8)

// Host observables (starvation simulation).
std::uint64_t last_reload_ms(); // virtual-clock time of last reload
std::uint64_t max_stall_ms();   // worst reload gap observed
bool starved();                 // now - last_reload >= hardware window

} // namespace watchdog
} // namespace slice
