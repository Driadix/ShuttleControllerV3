// RTOS task-mode harness driver (issue #54, static RTOS variant).
// Maps the harness steps onto the FreeRTOS task set:
// - safety_task (priority 5): safety_step + bumper notifications -> funnel
//   ForceStop intent (RTOS semantics: deferred to task context, preemption by
//   priority - distinct from hybrid ISR-context emission, captured by the
//   comparison);
// - control kernel task (priority 3, kernel-owned): drains schedule() work;
// - sensing_task (priority 4): sensing_step;
// - actuator_task (priority 2): actuator_step (CAN emission incl. min-ID
//   force-stop frame);
// - observability_task (priority 1): drains egress queues.
// Compiled only for V3_KERNEL_RTOS.
#pragma once

#include "proving/harness.h"

namespace slice
{
namespace proving
{
namespace rtos_harness
{

// Registers the harness tasks and starts the scheduler (never returns on
// success). `state` must outlive the scheduler (static storage).
void start(HarnessState& state);

} // namespace rtos_harness
} // namespace proving
} // namespace slice
