// Target entry point (design docs/execution-foundation-design-v3.md section
// 5.1, 6; sensing slice #63; safety slice docs/safety-authority-design-v3.md
// sections 3.1, 5.1). Target-only: the native host env excludes this file via
// build_src_filter (-<platform/main.cpp>), so no ARDUINO guard is needed.
//
// Wiring: adapters (tim2_clock, iwdg_watchdog, reset_cause, i2c_bus, can_bus,
// backup_marker) + Phase-1 stubs (KernelEvents no-op sink, safety Events no-op)
// -> kernel::init; the Sensing Service (#63) is scheduled as a bounded
// self-repeating step; the Safety Authority (#71) implements SafetySlot (tick
// on every step boundary, outside FIFO), and the Actuator emission step is
// armed by the safety slot when an intent is active.
// Phase 2 replaces the stubs: Observability Producer (#72) for KernelEvents /
// safety Events; the CAN peer/analyzer (#62) adds bus-level verification.
#include <Arduino.h>

#include "adapters/backup_marker.h"
#include "adapters/can_bus.h"
#include "adapters/i2c_bus.h"
#include "adapters/iwdg_watchdog.h"
#include "adapters/reset_cause.h"
#include "adapters/tim2_clock.h"
#include "domain/actuator.h"
#include "domain/queues.h"
#include "domain/runtime.h"
#include "domain/safety_authority.h"
#include "domain/semantic.h"
#include "domain/sensing.h"
#include "domain/slot.h"
#include "domain/subscriptions.h"
#include "platform/actuation_schedule.h"
#include "platform/admission_glue.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"
#include "platform/safety_glue.h"
#include "platform/sensing_schedule.h"

namespace
{

v3::Tim2Clock g_time;
v3::IwdgWatchdog g_hw;
v3::Stm32ResetCause g_reset;
v3::I2cBus g_i2c;
v3::sensing::SensingService g_sensing;
v3::CanBus g_can;
v3::BackupMarker g_marker;
v3::safety::SafetyAuthority g_sa;
v3::safety::ActuatorController g_actuator;
v3::safety::SafetySlotImpl g_safety_slot;

// Phase-1 KernelEvents stub (design section 2.4): no-op diagnostic sink.
// Phase 2: Observability Producer (#72). Contract: foreground-only calls.
class KernelEventsStub : public v3::KernelEvents
{
  public:
    void step_overrun(std::uint32_t) override {}
    void scheduler_gap(std::uint64_t) override {}
    void schedule_rejected() override {}
    void reset_cause(v3::ResetCause) override {}
};

// Phase-1 safety Events stub (design section 5.2): no-op. Phase 2: routed to
// Observability Producer (#72) - counters/events/traces. Emission is at the
// Safety Authority; counters live at the Producer (#43 §4).
class SafetyEventsStub : public v3::safety::SafetyAuthority::Events
{
  public:
    void health_changed(v3::safety::SafetyHealth, v3::safety::SafetyHealth,
                        v3::safety::DegradedClass, v3::safety::SafetyFault) override {}
    void stop_issued(v3::safety::StopProfile, std::uint32_t) override {}
    void can_failsafe(v3::CanErrorState) override {}
    void crash_marker_pending(std::uint32_t) override {}
};

KernelEventsStub g_events;
SafetyEventsStub g_safety_events;

// Semantic/runtime slice (#74, design docs/operation-runtime-design-v3.md):
// inbound queue, subscription registry, exclusive slot, type registry
// (EMPTY until Phase 3), Operation Runtime, Semantic Contract and the glue
// context. Phase-2-start wiring: gate ports are stubs (epoch/window/
// provisioning fixed; health is REAL - SafetyAuthority), events/outbound are
// no-op until #72 (Producer/Sink); the handshake grant is empty (authorityId
// 0) until #75 lands - all mutating requests answer HandshakeRequired, which
// is the correct pre-handshake behavior (#47 section 5.1).
v3::queue::InboundQueue g_inbound;
v3::subscription::Registry g_subscriptions;
v3::slot::ExclusiveSlot g_slot;
v3::semantic::TypeRegistry g_types;
v3::runtime::Runtime g_runtime;
v3::semantic::SemanticContract g_semantic;
v3::glue::SemanticContext g_glue_ctx;

// Gate stubs (#74, Phase-2 start; #76 lifecycle wiring replaces them).
class EpochStub : public v3::EpochSource
{
  public:
    std::uint32_t epoch() const override { return 1; } // fixed boot instance (#76 owns the real one)
};
class WindowStub : public v3::WindowSource
{
  public:
    v3::PlatformWindow window() const override { return v3::PlatformWindow::Serving; }
};
class ProvisioningStub : public v3::ProvisioningSource
{
  public:
    v3::ProvisioningStatus status() const override { return v3::ProvisioningStatus::Provisioned; }
};
class HealthAdapter : public v3::HealthSource // REAL: Safety Authority #71
{
  public:
    v3::safety::SafetyHealth health() const override { return g_sa.health(); }
};
class RuntimeEventsStub : public v3::RuntimeEvents
{
  public:
    void admission_rejected(std::uint8_t) override {}
    void request_duplicate(std::uint32_t, bool) override {}
    void transport_error(v3::codec::TransportError) override {}
    void queue_rejected(v3::codec::QueueClass) override {}
    void operation_started(std::uint32_t, std::uint16_t) override {}
    void operation_terminal(std::uint32_t, std::uint16_t, std::uint16_t) override {}
    void subscription_changed(std::uint16_t, bool) override {}
    void subscription_drop(std::uint8_t) override {}
};
class OutboundStub : public v3::OutboundControl
{
  public:
    bool enqueue(v3::codec::QueueClass, const std::uint8_t*, std::uint32_t) override { return true; }
};

EpochStub g_epoch;
WindowStub g_window;
ProvisioningStub g_provisioning;
HealthAdapter g_health;
RuntimeEventsStub g_runtime_events;
OutboundStub g_outbound;

} // namespace

void setup()
{
    const v3::kernel::KernelConfig cfg{
        &g_time,
        &g_hw,
        &g_events,
        &g_reset,
        &g_safety_slot,
    };
    v3::kernel::init(cfg);

    // Sensing slice (#63): I2C adapter init + service start.
    g_i2c.init();
    g_sensing.init(v3::sensing::SensingConfig{}, g_i2c);

    // Safety slice (#71): CAN adapter (normal mode; L4 loopback leg builds with
    // -DCAN_LOOPBACK), crash marker, Safety Authority (SafetySlot), Actuator.
    // diag = pinned .bram_safety mirror (SA/Actuator/CAN write their fields).
#ifdef CAN_LOOPBACK
    g_can.init(/*loopback=*/true, &v3::safety::safety_diag());
#else
    g_can.init(/*loopback=*/false, &v3::safety::safety_diag());
#endif
    g_sa.set_events(&g_safety_events); // перед init: crash_marker_pending долетает
    g_sa.init(v3::safety::SafetyAuthority::Config{}, &g_sensing, &g_can, &g_marker,
              &v3::safety::safety_diag());
    g_actuator.init(v3::safety::ActuatorController::Config{}, g_sa, &g_can,
                    &v3::safety::safety_diag());
    g_safety_slot.bind(&g_sa);
    v3::safety::actuation_bind(&g_actuator);

    // Sensing service: first slot due immediately (deadline now, within kernel window).
    const std::uint64_t now = v3::monotonic::now_ms();
    v3::kernel::schedule(&v3::sensing::schedule_tick, &g_sensing,
                         static_cast<std::uint32_t>(now));
    // Actuation step is armed by the safety slot on the first active intent
    // (idle = silence on the bus, Q7.1 A) - no startup schedule needed.

    // Semantic/runtime slice (#74): wire gate ports, events, outbound, the
    // single (empty) handshake grant and the runtime into the contract, then
    // arm the two self-repeating steps (inbound drain + runtime advance).
    g_subscriptions.init(/*profile=*/0, &g_runtime_events); // network_bridge default
    g_runtime.init(&g_epoch, &g_runtime_events, &g_slot);
    g_semantic.init(&g_epoch, &g_window, &g_health, &g_provisioning, &g_runtime_events,
                    &g_outbound, &g_runtime, &g_subscriptions, &g_types,
                    v3::semantic::Grant{}); // empty grant: HandshakeRequired until #75
    g_glue_ctx.inbound = &g_inbound;
    g_glue_ctx.semantic = &g_semantic;
    g_glue_ctx.runtime = &g_runtime;
    g_glue_ctx.events = &g_runtime_events;
    v3::kernel::schedule(&v3::glue::inbound_tick, &g_glue_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 1));
    v3::kernel::schedule(&v3::glue::runtime_tick, &g_glue_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 1));
}

void loop()
{
    v3::kernel::run(); // never returns (foreground WFI loop)
}
