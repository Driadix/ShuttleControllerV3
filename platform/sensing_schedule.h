// Composition-root glue for the sensing slice (design
// docs/sensing-slice-design-v3.md sections 5.3, 6; ticket #63). Host-buildable
// (no Arduino): schedules the SensingService bounded step via kernel::schedule
// and re-arms after every execution. The domain stays framework-free
// (dependencies inward, issue #43).
//
// The re-arm deadline is computed from a FRESH monotonic read AFTER the step:
// a long step (e.g. a stuck bus blocking the I2C transaction) must not make
// the deadline stale - the kernel rejects out-of-window deadlines silently.
// A rejected schedule is re-armed with an in-window now+1 deadline so the
// acquisition can never die without a trace (review MAJOR fix, #63).
#pragma once

#include <cstdint>

namespace v3
{
namespace sensing
{

// Kernel step callback (kernel::schedule target). Calls step(now) on the
// service, then re-schedules with a fresh deadline; on rejection, re-arms
// with now+1. Never returns (re-arms itself).
void schedule_tick(void* ctx);

} // namespace sensing
} // namespace v3
