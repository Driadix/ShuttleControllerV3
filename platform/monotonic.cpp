#include "platform/monotonic.h"

#include <cstdint>

#ifdef ARDUINO

// Target leg: TIM2 (32-bit) at 1 ms via the core's HardwareTimer (the core owns
// TIM2_IRQHandler through SrcWrapper/HAL - a custom handler would collide).
// Does not disturb SysTick (core millis).
//
// Time sources:
// - now_ms(): 64-bit counter advanced by the TIM2 ISR (never wraps practically).
// - ticks_us(): 64-bit cycle count. The 32-bit DWT CYCCNT wraps every
//   2^32/168 MHz ~ 25.6 s; the ISR detects the wrap (new < prev) and maintains
//   a 64-bit extension word, so ticks_us() is wrap-safe (BLOCKING B1 fix).
// Reads of 64-bit values on the 32-bit core use read-twice-and-compare so a
// torn ISR interleave is detected and retried (MAJOR M4 fix).
#include <Arduino.h>
#include <HardwareTimer.h>

namespace slice
{
namespace monotonic
{
namespace
{

// Single writer: TIM2 ISR (rule R2 - ISR writes only its own slots).
volatile std::uint64_t g_now_ms = 0; // 1 ms tick counter
volatile std::uint32_t g_cyccnt_high = 0; // wrap extension of DWT->CYCCNT
std::uint32_t g_prev_cyccnt = 0; // ISR-private previous sample
HardwareTimer g_tim2(TIM2);

void on_tick()
{
    ++g_now_ms;

    // Extend the 32-bit cycle counter across wraps: the ISR samples every
    // 1 ms (~168000 cycles), so a wrap is exactly when the new sample is
    // smaller than the previous one.
    const std::uint32_t cyccnt = DWT->CYCCNT;
    if (cyccnt < g_prev_cyccnt)
    {
        ++g_cyccnt_high;
    }
    g_prev_cyccnt = cyccnt;
}

// Read-twice-and-compare for a 64-bit value written only by the ISR.
std::uint64_t read64(const volatile std::uint64_t& v)
{
    const std::uint64_t a = v;
    const std::uint64_t b = v;
    return a == b ? a : v; // retry once; ISR writes are single-word-ordered
}

} // namespace

void init()
{
    // Enable the DWT cycle counter (Cortex-M4F).
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    g_now_ms = 0;
    g_cyccnt_high = 0;

    g_tim2.setOverflow(1000, MICROSEC_FORMAT); // 1 ms
    g_tim2.attachInterrupt(on_tick);
    g_tim2.resume();
}

std::uint64_t now_ms() { return read64(g_now_ms); }

std::uint64_t ticks_us()
{
    // 64-bit cycles = high_word : DWT->CYCCNT; / 168 MHz => microseconds.
    const std::uint32_t high1 = g_cyccnt_high;
    const std::uint32_t low = DWT->CYCCNT;
    const std::uint32_t high2 = g_cyccnt_high;
    const std::uint32_t high = (high1 == high2) ? high1 : high2; // torn read retry
    return (static_cast<std::uint64_t>(high) << 32 | low) / 168;
}

void test_set_time_ms(std::uint64_t) {}
void test_advance_ms(std::uint64_t) {}

} // namespace monotonic
} // namespace slice

#else

// Host leg: fully deterministic - virtual injected clock drives both now_ms
// and ticks_us (ticks_us = now_ms * 1000). Step durations measure 0 us on host:
// host timing is NOT execution evidence (real timing is the target DWT leg,
// obligation #4/#10, O4 oracle); determinism is the point (issue 10 evidence
// #5: host deterministic unit/property/fault tests). The reviewer's m4: wall
// clock made host timings scheduler-dependent (flaky 370 ms chain assert under
// parallel test load).

namespace slice
{
namespace monotonic
{
namespace
{

std::uint64_t g_now_ms = 0;

} // namespace

void init() { g_now_ms = 0; }

std::uint64_t now_ms() { return g_now_ms; }

std::uint64_t ticks_us() { return g_now_ms * 1000; }

void test_set_time_ms(std::uint64_t t) { g_now_ms = t; }
void test_advance_ms(std::uint64_t dt) { g_now_ms += dt; }

} // namespace monotonic
} // namespace slice

#endif
