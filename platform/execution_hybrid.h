// Hybrid kernel public surface (issue 10: preemptible critical path).
// Only compiled when V3_KERNEL_HYBRID is defined.
#pragma once

#include <cstdint>

namespace slice
{
namespace kernel
{

// Bumper ISR entry (target, priority 0): latches the force-stop request and
// preempts any running step; the frame is emitted from ISR context.
// Host: called by the fault injector; served at the next on_tick() boundary.
void force_stop_isr();

// Force-stop observables (obligation #3/#13).
std::uint64_t force_stop_latency_us();
std::uint32_t force_stop_count();

} // namespace kernel
} // namespace slice
