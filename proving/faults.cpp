#include "proving/faults.h"

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/safety_health.h"
#include "platform/execution_core.h"
#include "proving/harness.h"

#ifdef V3_KERNEL_HYBRID
#include "platform/execution_hybrid.h"
#endif
#ifdef V3_KERNEL_RTOS
#include "platform/execution_rtos.h"
#endif

namespace slice
{
namespace proving
{
namespace faults
{

void bumper_edge(HarnessState& state)
{
#ifdef V3_KERNEL_RTOS
    // RTOS: latch into the funnel + notify the highest-priority safety task
    // (deferred to task context with preemption by priority - the RTOS
    // semantics of the force-stop chain, obligation #3/#13).
    state.bumper_pending = true;
    kernel::rtos::bumper_notify_from_isr();
#elif defined(V3_KERNEL_HYBRID)
    // Preemptible channel: the hybrid kernel emits synchronously from ISR
    // context (obligation #3/#13).
    kernel::force_stop_isr();
#else
    // Cooperative: the bumper event is latched and applied to the funnel by
    // the sensing step at the next tick boundary (Q7.2 deferral semantics).
    state.bumper_pending = true;
#endif
}

void sensor_stale(HarnessState& state)
{
    // F2: force the sample age above T_fresh (300 ms). The sensing step stops
    // refreshing; the next safety step detects the staleness (INV-SENSING-FRESH).
    state.force_stale = true;
}

void link_loss(HarnessState& state)
{
    // F3: manual link loss - expire the lease immediately; the safety step
    // converts the expiry into a bounded CONTROLLED stop (obligation #6,
    // INV-LEASE-STOP: T_lease_stop = T_step + T_ramp).
    state.lease_expires_at_ms = kernel::now_ms();
}

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
