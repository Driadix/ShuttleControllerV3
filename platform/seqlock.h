// Wrap-safe seqlock snapshot primitive (design docs/execution-foundation-
// design-v3.md section 2.2, review fixes B1/M4 kept from the slice). The
// TIM2 ISR (adapters/tim2_clock) publishes the (cycles, prev_cyccnt) pair
// atomically via the sequence counter; readers snapshot and retry on mismatch
// or while odd. Pure C++, no Arduino: usable from host tests (T10 simulates
// the ISR writer) and from the target adapter alike.
#pragma once

#include <cstdint>

namespace v3
{
namespace detail
{

// Seqlock-guarded cycle state published by the clock ISR.
struct CycleSnapshot
{
    std::uint64_t cycles = 0;
    std::uint32_t prev_cyccnt = 0;
    std::uint32_t seq = 0; // even = consistent snapshot
};

// Reads a consistent (cycles, prev) pair: retries while the writer is
// mid-update (odd seq) or the snapshot changed between the two passes.
// Member reads of the volatile struct are volatile-qualified (no struct copy
// assignment). Returns the first consistent snapshot.
inline CycleSnapshot snapshot_cycle(const volatile CycleSnapshot& g)
{
    CycleSnapshot s1;
    CycleSnapshot s2;
    do
    {
        do
        {
            s1.seq = g.seq;
            s1.cycles = g.cycles;
            s1.prev_cyccnt = g.prev_cyccnt;
        } while ((s1.seq & 1u) != 0); // odd: writer in progress
        s2.seq = g.seq;
        s2.cycles = g.cycles;
        s2.prev_cyccnt = g.prev_cyccnt;
    } while (s1.seq != s2.seq || s1.cycles != s2.cycles || s1.prev_cyccnt != s2.prev_cyccnt);
    return s1;
}

} // namespace detail
} // namespace v3
