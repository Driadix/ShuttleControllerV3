// Target entry point (design docs/execution-foundation-design-v3.md section
// 5.1, 6; sensing slice #63; safety slice docs/safety-authority-design-v3.md
// sections 3.1, 5.1; observability docs/observability-design-v3.md, #72).
// Target-only: the native host env excludes this file via build_src_filter
// (-<platform/main.cpp>), so no ARDUINO guard is needed.
//
// Wiring: adapters (tim2_clock, iwdg_watchdog, reset_cause, i2c_bus, can_bus,
// backup_marker, uart_bridge, rtc_clock, identity) + Observability Producer/
// Sink (#72) -> kernel::init; the Sensing Service (#63) is scheduled as a
// bounded self-repeating step; the Safety Authority (#71) implements
// SafetySlot (tick on every step boundary, outside FIFO); the Actuator
// emission step is armed by the safety slot when an intent is active.
// Phase 2 (#72): KernelEvents / safety Events / RuntimeEvents / OutboundControl
// are implemented by the Observability Producer + Sink (UART TX on USART1).
#include <Arduino.h>

#include "adapters/backup_marker.h"
#include "adapters/can_bus.h"
#include "adapters/i2c_bus.h"
#include "adapters/identity.h"
#include "adapters/iwdg_watchdog.h"
#include "adapters/reset_cause.h"
#include "adapters/rtc_clock.h"
#include "adapters/tim2_clock.h"
#include "adapters/uart_bridge.h"
#include "domain/actuator.h"
#include "domain/observability.h"
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
#include "platform/observability_schedule.h"
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

// Observability (#72): Producer implements KernelEvents / RuntimeEvents /
// safety::Events; the Sink implements OutboundControl and drains to the UART.
// Adapters: uart_bridge (USART1 230400 8E1, TXE ring), rtc_clock (wall),
// identity (STM32 UID). Gate sources: health is REAL (SafetyAuthority);
// epoch/window/provisioning stay stubs until #76 (as in #74).
v3::UartBridge g_uart;
v3::RtcClock g_rtc;
v3::Stm32Identity g_identity;
v3::observability::Producer g_producer;
v3::observability::Sink g_sink;

// Semantic/runtime slice (#74, design docs/operation-runtime-design-v3.md):
// inbound queue, subscription registry, exclusive slot, type registry
// (EMPTY until Phase 3), Operation Runtime, Semantic Contract and the glue
// context. Phase-2-start wiring: gate ports are stubs (epoch/window/
// provisioning fixed; health is REAL - SafetyAuthority), events/outbound go
// to the Observability Producer/Sink (#72); the handshake grant is empty
// (authorityId 0) until #75 lands - all mutating requests answer
// HandshakeRequired, which is the correct pre-handshake behavior (#47 5.1).
v3::queue::InboundQueue g_inbound;
v3::subscription::Registry g_subscriptions;
v3::slot::ExclusiveSlot g_slot;
v3::semantic::TypeRegistry g_types;
v3::runtime::Runtime g_runtime;
v3::semantic::SemanticContract g_semantic;
v3::glue::SemanticContext g_glue_ctx;
v3::obsglue::ObsContext g_obs_ctx;

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

EpochStub g_epoch;
WindowStub g_window;
ProvisioningStub g_provisioning;
HealthAdapter g_health;

} // namespace

void setup()
{
    // Observability adapters FIRST (Producer/Sink need them before init).
    g_uart.init();
    g_rtc.init();

    // Observability Producer + Sink (#72): wire sources and the UART sink.
    // Producer.init BEFORE kernel::init: the kernel emits reset_cause() at
    // startup (KernelEvents), and the Producer must be initialized to record
    // the boot cause (design §3.3, #49 §13 crash record through reboot).
    g_producer.init(&g_epoch, &g_window, &g_health, &g_provisioning, &g_subscriptions,
                    &g_runtime, &g_sensing, &v3::safety::safety_diag(), &g_rtc, &g_identity,
                    &g_sink);
    g_sink.init(&g_uart, &g_subscriptions, &g_producer);

    const v3::kernel::KernelConfig cfg{
        &g_time,
        &g_hw,
        &g_producer, // KernelEvents: Producer (Phase 2, #72)
        &g_reset,
        &g_safety_slot,
    };
    v3::kernel::init(cfg);

    g_sa.set_events(&g_producer); // safety events -> Producer (Phase 2, #72)

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
    g_subscriptions.init(/*profile=*/0, &g_producer); // events -> Producer (#72)
    g_runtime.init(&g_epoch, &g_producer, &g_slot);   // runtime events -> Producer
    g_semantic.init(&g_epoch, &g_window, &g_health, &g_provisioning, &g_producer,
                    &g_sink, &g_runtime, &g_subscriptions, &g_types,
                    v3::semantic::Grant{}); // empty grant: HandshakeRequired until #75
    g_glue_ctx.inbound = &g_inbound;
    g_glue_ctx.semantic = &g_semantic;
    g_glue_ctx.runtime = &g_runtime;
    g_glue_ctx.events = &g_producer;
    v3::kernel::schedule(&v3::glue::inbound_tick, &g_glue_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 1));
    v3::kernel::schedule(&v3::glue::runtime_tick, &g_glue_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 1));

    // Observability glue (#72): sink drain each tick, telemetry at 300 ms
    // (bridge default), birth check on the same cadence.
    g_obs_ctx.producer = &g_producer;
    g_obs_ctx.sink = &g_sink;
    g_obs_ctx.subs = &g_subscriptions;
    v3::kernel::schedule(&v3::obsglue::sink_tick, &g_obs_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 1));
    v3::kernel::schedule(&v3::obsglue::telemetry_tick, &g_obs_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 300));
    v3::kernel::schedule(&v3::obsglue::birth_check, &g_obs_ctx,
                         static_cast<std::uint32_t>(v3::monotonic::now_ms() + 300));
}

void loop()
{
    v3::kernel::run(); // never returns (foreground WFI loop)
}

