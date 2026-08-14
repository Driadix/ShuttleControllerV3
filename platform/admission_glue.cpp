// Admission glue implementation (design docs/operation-runtime-design-v3.md
// section 3.1; ticket #74). Two self-repeating bounded kernel steps; the
// re-arm pattern (fresh deadline after execution, now+1 fallback on reject)
// mirrors platform/sensing_schedule.cpp (#63).
#include "platform/admission_glue.h"

#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace v3
{
namespace glue
{

void inbound_tick(void* ctx)
{
    auto* c = static_cast<SemanticContext*>(ctx);
    queue::Frame frame;
    if (c->inbound != nullptr && c->inbound->pop(queue::Class::Control, frame))
    {
        const codec::DecodeResult dr = codec::decode(frame.data, frame.len);
        if (dr.ok())
        {
            c->semantic->process_frame(dr.frame);
        }
        else if (c->events != nullptr)
        {
            c->events->transport_error(dr.error); // parse/drop observable (obs #7)
        }
    }

    // Re-arm from a FRESH monotonic read AFTER the step (never a stale
    // deadline); on rejection re-arm with an in-window now+1.
    const std::uint64_t now = monotonic::now_ms();
    if (kernel::schedule(inbound_tick, ctx, static_cast<std::uint32_t>(now + 1)) !=
        kernel::ScheduleResult::Ok)
    {
        const std::uint64_t again = monotonic::now_ms();
        (void)kernel::schedule(inbound_tick, ctx, static_cast<std::uint32_t>(again + 1));
    }
}

void runtime_tick(void* ctx)
{
    auto* c = static_cast<SemanticContext*>(ctx);
    if (c->runtime != nullptr)
    {
        c->runtime->advance(monotonic::now_ms());
    }

    // Re-arm from a FRESH monotonic read AFTER the step (never a stale
    // deadline); on rejection re-arm with an in-window now+1.
    const std::uint64_t now = monotonic::now_ms();
    if (kernel::schedule(runtime_tick, ctx, static_cast<std::uint32_t>(now + 1)) !=
        kernel::ScheduleResult::Ok)
    {
        const std::uint64_t again = monotonic::now_ms();
        (void)kernel::schedule(runtime_tick, ctx, static_cast<std::uint32_t>(again + 1));
    }
}

} // namespace glue
} // namespace v3
