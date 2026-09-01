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

// Re-arm helper: the kernel accepts only deadlines inside [now, now + T_step
// 10 ms] (execution foundation #85: one bounded step per tick, wrap-safe
// window). A far deadline like now+300 is rejected DeadlineOutOfWindow, so a
// slow-cadence step re-arms at now+1 EVERY tick and gates its own cadence
// internally (checked against a fresh monotonic read). This mirrors the
// sensing-glue fallback pattern (#63) but keeps the period honest: the wire
// cadence is the step's decision, not the scheduler's.
void rearm_next_tick(void (*fn)(void*), void* ctx)
{
    // now+1 is always inside [now, now + T_step 10 ms] unless the ring is
    // full; on QueueFull the step's own rearm had no effect and the kernel's
    // schedule_rejected() is observable (overload), no silent death.
    (void)kernel::schedule(fn, ctx,
                           static_cast<std::uint32_t>(monotonic::now_ms() + 1));
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
    rearm_next_tick(&sink_tick, ctx); // every tick
}

void telemetry_tick(void* ctx)
{
    static std::uint32_t s_last_emit_ms = 0; // wrap-safe modular compare
    const std::uint32_t now = static_cast<std::uint32_t>(monotonic::now_ms());
    auto* c = static_cast<ObsContext*>(ctx);
    if (c != nullptr && c->producer != nullptr &&
        static_cast<std::int32_t>(now - s_last_emit_ms) >=
            static_cast<std::int32_t>(kTelemetryDefaultMs))
    {
        s_last_emit_ms = now;
        c->producer->set_now(now);
        c->producer->emit_telemetry(); // interest gate inside (#49 section 9)
    }
    rearm_next_tick(&telemetry_tick, ctx);
}

void birth_check(void* ctx)
{
    static std::uint32_t s_last_check_ms = 0; // wrap-safe modular compare
    const std::uint32_t now = static_cast<std::uint32_t>(monotonic::now_ms());
    auto* c = static_cast<ObsContext*>(ctx);
    if (c != nullptr && c->subs != nullptr && c->producer != nullptr &&
        static_cast<std::int32_t>(now - s_last_check_ms) >=
            static_cast<std::int32_t>(kBirthCheckMs))
    {
        s_last_check_ms = now;
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
    rearm_next_tick(&birth_check, ctx);
}

} // namespace obsglue
} // namespace v3
