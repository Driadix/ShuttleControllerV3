// Composition-root glue for the semantic/runtime slice (design
// docs/operation-runtime-design-v3.md sections 5.1, 3.1; ticket #74).
// Host-buildable (no Arduino): schedules two self-repeating bounded steps -
// inbound drain (queue -> codec -> semantic) and runtime advance - via
// kernel::schedule, re-arming with a FRESH monotonic deadline after every
// execution (pattern platform/sensing_schedule.h, #63). A long step must not
// make the deadline stale - the kernel rejects out-of-window deadlines; on
// rejection the step is re-armed with now+1 so the pipeline can never die
// without a trace.
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/queues.h"
#include "domain/runtime.h"
#include "domain/semantic.h"

namespace v3
{
namespace glue
{

struct SemanticContext
{
    queue::InboundQueue* inbound = nullptr;
    semantic::SemanticContract* semantic = nullptr;
    runtime::Runtime* runtime = nullptr;
    RuntimeEvents* events = nullptr;
};

// Kernel step: pops at most one Control frame, decodes it and feeds the
// Semantic Contract; re-arms itself (never returns).
void inbound_tick(void* ctx);

// Kernel step: advances the Operation Runtime by at most one due instance
// (bounded, <= T_step per instance); re-arms itself (never returns).
void runtime_tick(void* ctx);

} // namespace glue
} // namespace v3
