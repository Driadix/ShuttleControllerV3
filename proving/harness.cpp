#include "proving/harness.h"

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/ports.h"
#include "domain/safety_health.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
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
    const std::uint64_t t0 = monotonic::ticks_us();

    // F1 (cooperative variant): latch the bumper into the funnel. The ForceStop
    // intent is applied here so the actuator step emits the min-ID frame at the
    // next boundary (Q7.2: first edge latches, repeated edges collapse).
    if (s->bumper_pending)
    {
        Intent fs{};
        fs.kind = IntentKind::ForceStop;
        fs.source = IntentSource::Safety;
        fs.seq = static_cast<std::uint32_t>(now);
        s->arb.apply(fs);
        s->bumper_pending = false;
        if (s->measurement != nullptr)
        {
            s->stop_intent_at_us = monotonic::ticks_us(); // trigger
            s->stop_pending_trace = true;
        }
    }

    // Freshness model: refresh the sample every round-robin cycle. The sample
    // age is the time since the last successful refresh. Fault F2 (force_stale)
    // stops the refresh so the age grows past T_fresh.
    if (!s->force_stale && now - s->last_sample_at_ms >= kSensingCadenceMs)
    {
        s->last_sample_at_ms = now;
    }
    s->sample_age_ms = now - s->last_sample_at_ms;

    if (s->measurement != nullptr)
    {
        s->measurement->metric(2).record(monotonic::ticks_us() - t0); // T_check_jitter class
    }
}

void safety_step(void* ctx)
{
    auto* s = static_cast<HarnessState*>(ctx);
    const std::uint64_t now = kernel::now_ms();
    const std::uint64_t t0 = monotonic::ticks_us();

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
            s->stop_intent_at_us = monotonic::ticks_us(); // staleness detected
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
            s->stop_intent_at_us = monotonic::ticks_us();
            s->stop_pending_trace = true;
        }
        s->manual_held = false;
    }

    if (s->measurement != nullptr)
    {
        s->measurement->metric(2).record(monotonic::ticks_us() - t0); // T_arb class
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
    const std::uint64_t t0 = monotonic::ticks_us();

    CanPort::Frame f = {};
    if (current.kind == IntentKind::ForceStop)
    {
        s->can->force_stop_tx(); // dedicated mailbox (obligation #13)
        f.id = 0x00000001;       // min extended ID
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

    // Trace: trigger (staleness/lease/bumper detected) -> safe output (CAN
    // emitted), measured in us. T_eso = T_check_jitter + T_arb + T_emit
    // (obligation #1, budget 70 ms analytical; issue #48 section 2).
    if (s->stop_pending_trace && s->measurement != nullptr)
    {
        const std::uint64_t now_us = monotonic::ticks_us();
        s->last_stop_emitted_at_us = now_us;
        s->measurement->record_trace_us(s->stop_intent_at_us, now_us);
        s->measurement->metric(1).record(now_us - s->stop_intent_at_us);
        s->stop_pending_trace = false;
    }
    else
    {
        s->last_stop_emitted_at_us = monotonic::ticks_us();
    }

    if (s->measurement != nullptr)
    {
        s->measurement->metric(2).record(monotonic::ticks_us() - t0); // T_emit class
    }
}

void schedule_standard_steps(HarnessState& state)
{
    // All three steps run in the SAME tick (deadline 0 = due at the next
    // on_tick): the C1 chain sensing -> safety -> arbitration -> actuation is
    // a same-tick composition, so the trigger->output trace (T_eso) is
    // measured within one bounded step, not spread across ticks.
    kernel::schedule(sensing_step, &state, 0);
    kernel::schedule(safety_step, &state, 0);
    kernel::schedule(actuator_step, &state, 0);
}

} // namespace proving
} // namespace slice
