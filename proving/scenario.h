// Scenario runner: drives the harness under a fixed workload for N ticks and
// emits the measurement record (begin_run -> steps/loads -> finalize).
// The same code runs on host (deterministic injected clock) and on target
// (real TIM2 tick); clock injection calls are no-ops on target.
#pragma once

#include <cstdint>

#include "proving/harness.h"
#include "proving/workload.h"

namespace slice
{
namespace proving
{

// Runs the standard steps + the active load generators from `wl` for `ticks`
// ticks, then finalizes the measurement record (observed maxima + worst trace
// with workload metadata, issue #52 section 6.3).
void run_scenario(HarnessState& state, const Workload& wl, std::uint32_t ticks);

} // namespace proving
} // namespace slice
