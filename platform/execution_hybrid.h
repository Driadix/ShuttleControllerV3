// Hybrid kernel public surface (issue 10: preemptible critical path).
// Only compiled when V3_KERNEL_HYBRID is defined.
#pragma once

#include <cstdint>

namespace slice
{
namespace kernel
{

// Bumper ISR entry (target, priority 0): emits the min-ID force-stop frame
// DIRECTLY through the registered handler - preempts any running step
// (T_fs = T_isr + T_mailbox, C4; obligation #3/#13). The handler must be
// ISR-safe (bxCAN mailbox write is register-level). Host: called by the fault
// injector; the emission is synchronous at the call site (simulated
// preemption; real preemption timing is target evidence).
void force_stop_isr();

// Force-stop observables (obligation #3/#13).
std::uint64_t force_stop_latency_us();
std::uint32_t force_stop_count();

} // namespace kernel
} // namespace slice
