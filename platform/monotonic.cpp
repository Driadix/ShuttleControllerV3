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
HardwareTimer g_tim2(TIM2);

// Seqlock-guarded cycle state. The ISR publishes (cycles64, prev_cyccnt)
// atomically via the sequence counter (even = consistent); readers snapshot
// and retry on mismatch or while odd. cycles64 accumulates wrap-safe uint32
// deltas, so it is monotonic and epoch-safe; the live CYCCNT read adds
// sub-ms precision (the 1 ms quantization alone would collapse ISR-latency
// and step-duration measurements into 0/1000 us bins).
struct CycleState
{
    std::uint64_t cycles = 0;
    std::uint32_t prev_cyccnt = 0;
    std::uint32_t seq = 0; // even = consistent snapshot
};
volatile CycleState g_cycle;

void on_tick()
{
    ++g_now_ms;

    // Publish (cycles, prev) as a consistent pair: seq odd while writing.
    const std::uint32_t cyccnt = DWT->CYCCNT;
    ++g_cycle.seq; // odd: writer in progress
    g_cycle.cycles += static_cast<std::uint32_t>(cyccnt - g_cycle.prev_cyccnt);
    g_cycle.prev_cyccnt = cyccnt;
    ++g_cycle.seq; // even: consistent
}

// Read-twice-and-compare with bounded retry: the ISR is the only writer, so a
// torn interleave is detected by mismatched reads and retried (single-word
// writes are atomic on Cortex-M4).
std::uint64_t read64(const volatile std::uint64_t& v)
{
    std::uint64_t a = v;
    std::uint64_t b = v;
    while (a != b)
    {
        a = v;
        b = v;
    }
    return a;
}

} // namespace

void init()
{
    // Enable the DWT cycle counter (Cortex-M4F).
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    g_now_ms = 0;
    g_cycle.seq = 0;
    g_cycle.cycles = 0;
    g_cycle.prev_cyccnt = 0;

    g_tim2.setOverflow(1000, MICROSEC_FORMAT); // 1 ms
    g_tim2.attachInterrupt(on_tick);
    g_tim2.resume();
}

std::uint64_t now_ms() { return read64(g_now_ms); }

std::uint64_t ticks_us()
{
    // Seqlock snapshot: a consistent (cycles, prev) pair, then the live
    // CYCCNT delta for sub-ms precision. Retry while the ISR is mid-write
    // (odd seq) or the snapshot changed (seq mismatch). Member reads of the
    // volatile struct are volatile-qualified (no struct copy assignment).
    const auto snapshot = []() {
        CycleState s;
        s.seq = g_cycle.seq;
        s.cycles = g_cycle.cycles;
        s.prev_cyccnt = g_cycle.prev_cyccnt;
        return s;
    };

    CycleState s1;
    CycleState s2;
    do
    {
        do
        {
            s1 = snapshot();
        } while ((s1.seq & 1u) != 0); // odd: writer in progress
        s2 = snapshot();
    } while (s1.seq != s2.seq || s1.cycles != s2.cycles || s1.prev_cyccnt != s2.prev_cyccnt);

    const std::uint32_t live = DWT->CYCCNT;
    const std::uint64_t cycles = s1.cycles + static_cast<std::uint32_t>(live - s1.prev_cyccnt);
    return cycles / 168; // 168 MHz => us
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
