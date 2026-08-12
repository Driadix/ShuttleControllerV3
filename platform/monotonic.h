// Monotonic time source: 1 ms tick (issue #48 section 9: monotonic, wrap-safe,
// NTP-jump immune - all domain timeouts count from monotonic).
//
// Target: TIM2 (32-bit) 1 ms tick ISR increments a 64-bit counter; high
// resolution via DWT CYCCNT (Cortex-M4F, see measurement.md).
// Host: virtual injected clock for deterministic tests (set/advance); ticks_us
// falls back to a real steady clock for host benchmark runs (non-deterministic).
#pragma once

#include <cstdint>

namespace slice
{
namespace monotonic
{

void init();

std::uint64_t now_ms();
std::uint64_t ticks_us(); // high-resolution source (DWT on target)

// Host-only test injection (deterministic core, issue 10).
void test_set_time_ms(std::uint64_t t);
void test_advance_ms(std::uint64_t dt);

} // namespace monotonic
} // namespace slice
