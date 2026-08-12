// Harness state and bounded steps for the proving slice. This is the C1 chain
// made measurable: sensing (freshness) -> Safety Authority -> arbitration ->
// actuation (CAN emission). The measurement recorder captures trigger->output
// traces for obligation #1 (T_eso), and per-step durations for #8.
#pragma once

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/ports.h"
#include "domain/queues.h"
#include "domain/safety_health.h"
#include "proving/measurement.h"
#include "proving/workload.h"

namespace slice
{
namespace proving
{

// Shared harness state. Single-writer ownership per field (issue #43 section 4):
// - sample_age_ms: written by the sensing step (and fault injectors), read by Safety.
// - output: written by the actuator step only.
struct HarnessState
{
    SafetyHealth health;
    Arbitration arb;
    CanPort* can = nullptr;
    QueueClasses* queues = nullptr; // observability egress queues (log storm L4)
    Measurement* measurement = nullptr;

    // Freshness model (ToF class, issue #48 section 5): the sensing step
    // refreshes this each round-robin cycle; fault F2 sets it above T_fresh.
    std::uint64_t sample_age_ms = 0;
    std::uint64_t last_sample_at_ms = 0;

    // Commanded motion (activity intent active). INV-SENSING-FRESH: motion
    // requires fresh directional sensing; staleness under motion => stop.
    bool motion_commanded = false;

    // F1 bumper injection (cooperative variant): latched into the funnel by the
    // sensing step at the next boundary (Q7.2 deferral semantics on target).
    // Hybrid variant uses kernel::force_stop_isr() instead (preemptible).
    bool bumper_pending = false;

    // F3 lease model: manual hold-to-run; expiry => CONTROLLED stop.
    std::uint64_t lease_expires_at_ms = 0;
    bool manual_held = false;

    // F2 stale injection: when set, the sensing step stops refreshing the
    // sample so its age grows past T_fresh (staleness under motion).
    bool force_stale = false;

    // Trace bookkeeping for obligation #1 (T_eso, measured in us).
    std::uint64_t stop_intent_at_us = 0; // when Safety issued the stop intent
    std::uint64_t last_stop_emitted_at_us = 0;
    bool stop_pending_trace = false;
};

// Sensing step: advances the freshness model (round-robin cadence 8 ms class,
// issue #48 section 5). Bounded, far below T_step.
void sensing_step(void* ctx);

// Safety step: ticks the health model, maps freshness loss to a stop intent
// through the arbitration funnel (INV-SENSING-FRESH), checks lease (F3).
void safety_step(void* ctx);

// Actuator step: reads the single current intent and emits the corresponding
// CAN frame (or force-stop). Records the trigger->output trace.
void actuator_step(void* ctx);

// Schedules the standard scenario steps for one tick.
void schedule_standard_steps(HarnessState& state);

} // namespace proving
} // namespace slice
