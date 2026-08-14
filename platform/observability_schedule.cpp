// Observability glue implementation (design docs/observability-design-v3.md
// sections 3.1-3.4, 5.3; ticket #72). Self-repeating kernel steps, host-buildable.
#include "platform/observability_schedule.h"

#include "domain/subscriptions.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace v3
{
namespace obsglue
{

namespace
{

constexpr std::uint32_t kTelemetryDefaultMs = 300; // bridge default (#49 section 9)
constexpr std::uint32_t kBirthCheckMs = 300;       // birth checked on the same cadence

// Re-arms the step with a fresh deadline; on rejection (out-of-window, e.g. a
// long previous step) re-arms with now+1 so the pipeline never dies silently.
void rearm(void (*fn)(void*), void* ctx)
{
    const std::uint64_t now = monotonic::now_ms();
    if (kernel::schedule(fn, ctx, static_cast<std::uint32_t>(now + 1)) !=
        kernel::ScheduleResult::Ok)
    {
        (void)kernel::schedule(fn, ctx, static_cast<std::uint32_t>(now + 1));
    }
}

} // namespace

void sink_tick(void* ctx)
{
    auto* c = static_cast<ObsContext*>(ctx);
    if (c != nullptr && c->sink != nullptr)
    {
        c->sink->tick(); // never blocks; per-tick caps/priorities inside
    }
    rearm(&sink_tick, ctx);
}

void telemetry_tick(void* ctx)
{
    auto* c = static_cast<ObsContext*>(ctx);
    if (c != nullptr && c->producer != nullptr)
    {
        c->producer->set_now(static_cast<std::uint32_t>(monotonic::now_ms()));
        c->producer->emit_telemetry(); // interest gate inside (#49 section 9)
    }
    rearm(&telemetry_tick, ctx);
}

void birth_check(void* ctx)
{
    auto* c = static_cast<ObsContext*>(ctx);
    if (c != nullptr && c->subs != nullptr && c->producer != nullptr)
    {
        // Birth push on (re)subscribe (#49 section 2.6): bounded scan of the
        // registry slots (<= BridgeCap 8); push_birth handles birth_sent().
        for (std::uint8_t authority = 1; authority <= 16; ++authority)
        {
            if (c->subs->birth_pending(authority))
            {
                c->producer->push_birth(authority);
            }
        }
    }
    rearm(&birth_check, ctx);
}

} // namespace obsglue
} // namespace v3
