// TIM2 clock adapter implementation (target-only, Arduino Core). The ISR does
// exactly two things (design section 3.1, rule R2):
//   1. ++g_now_ms (64-bit tick);
//   2. seqlock publish of the CYCCNT wrap-safe extension.
// No scheduling, no reload, no events from the ISR (T15 / include-lint).
//
// Time sources:
// - now_ms: 64-bit counter advanced by the TIM2 ISR (never wraps practically).
// - ticks_us: 64-bit cycle count; the 32-bit DWT CYCCNT wraps every
//   2^32/168 MHz ~ 25.6 s; the ISR detects the wrap and maintains a 64-bit
//   extension word, so ticks_us is wrap-safe (BLOCKING B1 fix).
// 64-bit reads on the 32-bit core: seqlock + PRIMASK-critical read64 (review
// B1/M4 fixes kept from the slice: read-twice alone is insufficient).
#include "adapters/tim2_clock.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <HardwareTimer.h>

#include "platform/seqlock.h"

namespace v3
{
namespace
{

// Single writer: TIM2 ISR (rule R2 - ISR writes only its own slots).
volatile std::uint64_t g_now_ms = 0; // 1 ms tick counter
HardwareTimer g_tim2(TIM2);

// Seqlock-guarded cycle state (v3::detail::CycleSnapshot). The ISR publishes
// (cycles64, prev_cyccnt) atomically via the sequence counter (even =
// consistent); readers snapshot and retry on mismatch or while odd (shared
// primitive platform/seqlock.h, review B1/M4 fixes).
volatile detail::CycleSnapshot g_cycle;

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

// Atomic 64-bit read via a brief PRIMASK critical section: the only writers
// are ISRs (TIM2 tick increment, bumper edge), all short. Read-twice-and-
// compare is NOT sufficient - an ISR write can land at the same half-word
// boundary on both reads, yielding a consistently torn value (review B1/M4
// follow-up). Disabling interrupts around the read is bounded and correct.
std::uint64_t read64(const volatile std::uint64_t& v)
{
    const std::uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const std::uint64_t val = v;
    __set_PRIMASK(primask);
    return val;
}

} // namespace

void Tim2Clock::init_tick()
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

std::uint64_t Tim2Clock::raw_now_ms() { return read64(g_now_ms); }

std::uint64_t Tim2Clock::raw_ticks_us()
{
    // Seqlock snapshot: a consistent (cycles, prev) pair, then the live
    // CYCCNT delta for sub-ms precision (shared primitive, platform/seqlock.h).
    const detail::CycleSnapshot s = detail::snapshot_cycle(g_cycle);

    const std::uint32_t live = DWT->CYCCNT;
    const std::uint64_t cycles = s.cycles + static_cast<std::uint32_t>(live - s.prev_cyccnt);
    return cycles / 168; // 168 MHz => us
}

} // namespace v3

#endif // ARDUINO
