#include "proving/faults.h"

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/safety_health.h"
#include "platform/execution_core.h"

#ifdef V3_KERNEL_HYBRID
#include "platform/execution_hybrid.h"
#endif

namespace slice
{
namespace proving
{
namespace faults
{

void bumper_edge()
{
#ifdef V3_KERNEL_HYBRID
    kernel::force_stop_isr(); // preemptible channel (ISR priority on target)
#else
    // Cooperative: the bumper event is latched into the arbitration funnel by
    // the sensing step at the next tick boundary (no preemption by design).
    // The harness injects the intent through the normal step path.
#endif
}

void sensor_stale(HarnessState& state)
{
    // F2: force the sample age above T_fresh (300 ms). The sensing step stops
    // refreshing; the next safety step detects the staleness (INV-SENSING-FRESH).
    state.force_stale = true;
}

void link_loss() {}

void stop_command(Arbitration& arb)
{
    Intent stop{};
    stop.kind = IntentKind::Stop;
    stop.source = IntentSource::Safety;
    stop.stop_profile = StopProfile::Controlled;
    (void)arb.apply(stop);
}

void critical_stall(void*)
{
    // Never returns: simulates an unbounded callback (issue 10 failure
    // condition: one wrongly long callback delays the whole safety path).
    for (;;)
    {
    }
}

} // namespace faults
} // namespace proving
} // namespace slice
