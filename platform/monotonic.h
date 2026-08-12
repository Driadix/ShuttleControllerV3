// Monotonic time source: 1 ms tick (issue #48 section 9: monotonic, wrap-safe,
// NTP-jump immune - all domain timeouts count from monotonic).
//
// Target: TIM2 (32-bit) 1 ms tick ISR (via the core's HardwareTimer) advances a
// 64-bit counter; the ISR also extends the 32-bit DWT CYCCNT into a 64-bit
// cycle count, so ticks_us() never wraps (issue #45 §7.2 / obligation #3).
// 64-bit reads on the 32-bit core are protected from ISR-torn values by
// read-twice-and-compare (rule R2: ISR writes only its own slots; readers
// tolerate torn snapshots).
//
// Host: virtual injected clock drives both now_ms and ticks_us - fully
// deterministic (issue 10 evidence #5). Real timing evidence is target-only
// (DWT leg); host step durations are 0 us by design (documented in
// proving-slice-v3.md section 8).
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
