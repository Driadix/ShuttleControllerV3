#include "proving/harness.h"

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/ports.h"
#include "domain/safety_health.h"
#include "platform/execution_core.h"
#include "proving/measurement.h"

namespace slice
{
namespace proving
{

namespace
{

constexpr std::uint32_t kSensingCadenceMs = 8; // ToF round-robin slot (issue #48 section 5)

// T_fresh budget chain (issue #48 section 2): C1a requires
// T_sample_worst + margin < 300 ms. The harness asserts the freshness model
// stays inside T_sample_worst_budget; measured via traces (obligation #1).
constexpr std::uint64_t kTfreshMs = SafetyHealth::T_fresh_ms;

} // namespace

void sensing_step(void* ctx)
{
    auto* s = static_cast<HarnessState*>(ctx);
    const std::uint64_t now = kernel::now_ms();

    // Freshness model: refresh the sample every round-robin cycle. The sample
    // age is the time since the last successful refresh. Fault F2 (force_stale)
    // stops the refresh so the age grows past T_fresh.
    if (!s->force_stale && now - s->last_sample_at_ms >= kSensingCadenceMs)
    {
        s->last_sample_at_ms = now;
    }
    s->sample_age_ms = now - s->last_sample_at_ms;
}

void safety_step(void* ctx)
{
    auto* s = static_cast<HarnessState*>(ctx);
    const std::uint64_t now = kernel::now_ms();

    s->health.tick(now, s->sample_age_ms);

    // INV-SENSING-FRESH: freshness loss under commanded motion => IMMEDIATE
    // stop intent. Staleness while stationary degrades health but does not
    // force a stop (the motion gate is the commanded state, not the health
    // state derived from it).
    const bool fresh = s->sample_age_ms <= kTfreshMs;
    if (!fresh && s->motion_commanded)
    {
        Intent stop{};
        stop.kind = IntentKind::Stop;
        stop.source = IntentSource::Safety;
        stop.stop_profile = StopProfile::Immediate;
        stop.seq = static_cast<std::uint32_t>(now);
        s->arb.apply(stop);
        if (s->measurement != nullptr)
        {
            s->stop_intent_at_ms = now; // trigger timestamp (staleness detected)
            s->stop_pending_trace = true;
        }
    }

    // F3: manual lease expiry => CONTROLLED stop (obligation #6).
    if (s->manual_held && now >= s->lease_expires_at_ms)
    {
        Intent stop{};
        stop.kind = IntentKind::Stop;
        stop.source = IntentSource::Safety;
        stop.stop_profile = StopProfile::Controlled;
        s->arb.apply(stop);
        if (s->measurement != nullptr)
        {
            s->stop_intent_at_ms = now;
            s->stop_pending_trace = true;
        }
        s->manual_held = false;
    }
}

void actuator_step(void* ctx)
{
    auto* s = static_cast<HarnessState*>(ctx);
    if (s->can == nullptr)
    {
        return;
    }
    const Intent& current = s->arb.current();
    const std::uint64_t now = kernel::now_ms();

    CanPort::Frame f = {};
    if (current.kind == IntentKind::ForceStop)
    {
        s->can->force_stop_tx();
        f.id = 0x00000001; // min extended ID (obligation #13)
    }
    else
    {
        f.id = 0x100; // velocity setpoint / stop frame (issue #43 section 4)
        f.len = 8;
        if (current.kind == IntentKind::Stop)
        {
            // zero-speed frame (IMMEDIATE zero at next emission)
        }
    }
    s->can->tx(f);
    s->last_stop_emitted_at_ms = now;

    // Trace: trigger (staleness/lease detected) -> safe output (CAN emitted).
    if (s->stop_pending_trace && s->measurement != nullptr)
    {
        s->measurement->record_trace(s->stop_intent_at_ms, now);
        s->stop_pending_trace = false;
    }
}

void schedule_standard_steps(HarnessState& state)
{
    kernel::schedule(sensing_step, &state, 1);
    kernel::schedule(safety_step, &state, 2);
    kernel::schedule(actuator_step, &state, 3);
}

} // namespace proving
} // namespace slice
