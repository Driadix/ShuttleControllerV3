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

// Re-arms the step with a fresh deadline. Pattern platform/sensing_schedule.h
// (#63): PRIMARY deadline = now + period (cadence preserved), FALLBACK = now+1
// only when the primary is rejected (out-of-window after a long step) so the
// pipeline never dies silently. Every-step steps pass period=1.
void rearm(void (*fn)(void*), void* ctx, std::uint32_t period_ms)
{
    const std::uint64_t now = monotonic::now_ms();
    if (kernel::schedule(fn, ctx, static_cast<std::uint32_t>(now + period_ms)) !=
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
    // Uptime counter lives at the Producer (single-writer, #43 s4); the glue
    // drives it once per tick (Producer advances it only on the second edge).
    if (c != nullptr && c->producer != nullptr)
    {
        c->producer->update_uptime(static_cast<std::uint32_t>(monotonic::now_ms()));
    }
    rearm(&sink_tick, ctx, /*period_ms=*/1); // every tick
}

void telemetry_tick(void* ctx)
{
    auto* c = static_cast<ObsContext*>(ctx);
    if (c != nullptr && c->producer != nullptr)
    {
        c->producer->set_now(static_cast<std::uint32_t>(monotonic::now_ms()));
        c->producer->emit_telemetry(); // interest gate inside (#49 section 9)
    }
    rearm(&telemetry_tick, ctx, kTelemetryDefaultMs); // bridge default 300 ms
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
    rearm(&birth_check, ctx, kBirthCheckMs); // same cadence as telemetry
}

} // namespace obsglue
} // namespace v3
