// Synthetic load generators (issue 10 mandatory loads; budgets #48 section 7).
// Each generator is a bounded kernel step that injects work/traffic; the
// harness schedules them. Host leg is deterministic; target leg adds real
// peripheral activity behind the same ports.
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/queues.h"
#include "platform/execution_core.h"

namespace slice
{
namespace proving
{
namespace loads
{

// L7: schedules `count` bounded synthetic operation steps (max-load generator).
// Returns the number actually scheduled (full step queue => observable overload).
std::uint32_t schedule_operation_steps(std::uint32_t count);

// L4: log storm - pushes `count` log records into the logs queue; drop-newest
// policy applies, overflow counted (obligation #12: no step exceeds T_step).
void log_storm(QueueClasses& queues, std::uint32_t count);

// L1: CAN flood - injects `count` frames into the CAN RX path (obligation #13:
// RX overflow + control plane stays alive under > 64 frames/tick).
void can_flood(CanPort& can, std::uint32_t count);

// L2: fills the CAN TX log past its per-tick budget (obligation #12/#13:
// bounded TX, no blocking).
void tx_backpressure(CanPort& can, std::uint32_t count);

// Busy-work step used as the bounded-step generator body (measures step
// duration; stays far below T_step on host).
void busy_step(void* ctx);

} // namespace loads
} // namespace proving
} // namespace slice
