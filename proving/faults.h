// Fault injectors (issue 10 mandatory fault cases). Host leg: direct calls.
// Target leg: the same entry points are wired to GPIO/ISR paths by the harness.
#pragma once

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/safety_health.h"
#include "proving/harness.h"

namespace slice
{
namespace proving
{
namespace faults
{

// F1: bumper edge. Hybrid variant: enters the preemptible force-stop channel
// (kernel::force_stop_isr). Cooperative variant: the same semantic is a latch
// processed at the next tick boundary - the comparison records the difference.
void bumper_edge();

// F2: sensor stale - stops sensing refreshes so the sample age exceeds
// T_fresh. The Safety Authority must produce a stop intent (obligation #1/#2).
void sensor_stale(HarnessState& state);

// F3: manual link loss - lease expiry; must produce a bounded CONTROLLED stop
// (obligation #6).
void link_loss();

// F4: external STOP command through the arbitration funnel.
void stop_command(Arbitration& arb);

// F5: deliberate critical stall - a step that never returns (starvation test).
// Target: watchdog resets in the hardware window. Host: watchdog::starved()
// becomes true when ticks stop being driven. NEVER scheduled in normal runs.
void critical_stall(void* ctx);

} // namespace faults
} // namespace proving
} // namespace slice
