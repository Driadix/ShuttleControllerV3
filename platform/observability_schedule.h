// Composition-root glue for the observability slice (design
// docs/observability-design-v3.md sections 5.1, 3.1-3.4; ticket #72).
// Host-buildable (no Arduino): schedules three self-repeating bounded steps -
// sink drain, telemetry emission (bridge 300 ms default / subscription
// cadence) and birth check - via kernel::schedule, re-arming with a FRESH
// monotonic deadline after every execution (pattern platform/sensing_schedule.h,
// #63; admission_glue.h, #74). A long step must not make the deadline stale -
// on rejection the step is re-armed with now+1 so the pipeline never dies.
#pragma once

#include <cstdint>

#include "domain/observability.h"

namespace v3
{
namespace obsglue
{

struct ObsContext
{
    observability::Producer* producer = nullptr;
    observability::Sink* sink = nullptr;
    subscription::Registry* subs = nullptr; // birth check (interest/birth_pending)
};

// Kernel step: drains the Sink (per-tick priorities/caps, never blocks).
// Re-arms itself. Call every tick.
void sink_tick(void* ctx);

// Kernel step: emits a telemetry record if the cadence is due (bridge default
// 300 ms, #49 section 9) and interest holds; re-arms itself.
void telemetry_tick(void* ctx);

// Kernel step: checks subscription birth flags and pushes the birth snapshot
// (#49 section 2.6); re-arms itself. Runs on the same cadence as telemetry.
void birth_check(void* ctx);

} // namespace obsglue
} // namespace v3
