// Target entry point (design docs/execution-foundation-design-v3.md section
// 5.1, 6; sensing slice docs/sensing-slice-design-v3.md section 5). Target-only:
// the native host env excludes this file via build_src_filter
// (-<platform/main.cpp>), so no ARDUINO guard is needed.
//
// Wiring: adapters (tim2_clock, iwdg_watchdog, reset_cause, i2c_bus) +
// Phase-1 stubs (KernelEvents no-op sink, SafetySlot no-op) -> kernel::init;
// then the Sensing Service (#63) is scheduled as a bounded self-repeating
// step (composition root owns the scheduling - the domain stays framework
// free, dependencies inward, issue #43).
// Phase 2 replaces the stubs: Observability Producer (#72) for KernelEvents,
// Safety Authority (#71) for SafetySlot. The KernelEvents stub is no-op (not
// UART): diagnostics go through the reset-cause record; UART is its own slice.
#include <Arduino.h>

#include "adapters/i2c_bus.h"
#include "adapters/iwdg_watchdog.h"
#include "adapters/reset_cause.h"
#include "adapters/tim2_clock.h"
#include "domain/sensing.h"
#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace
{

v3::Tim2Clock g_time;
v3::IwdgWatchdog g_hw;
v3::Stm32ResetCause g_reset;
v3::I2cBus g_i2c;
v3::sensing::SensingService g_sensing;

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

// Phase-1 SafetySlot stub (design section 2.5): no-op. Phase 2+: Safety
// Authority (#71) - the single arbitration funnel (issue #43 section 3.1).
// The kernel calls tick() on every step boundary regardless (mechanism is in
// scope #70, the safety policy itself is module #71).
class SafetySlotStub : public v3::SafetySlot
{
  public:
    void tick(std::uint64_t) override {}
};

KernelEventsStub g_events;
SafetySlotStub g_safety;

// Composition-root glue for the sensing slice: one bounded step per
// invocation (deadline now + tof_slot_ms, in the kernel window), re-scheduled
// after every execution. The service itself is framework-free (#43).
void sensing_tick(void* ctx)
{
    auto* svc = static_cast<v3::sensing::SensingService*>(ctx);
    const std::uint64_t now = v3::monotonic::now_ms();
    svc->step(now);
    v3::kernel::schedule(&sensing_tick, ctx,
                         static_cast<std::uint32_t>(now) + svc->next_step_ms());
}

} // namespace

void setup()
{
    const v3::kernel::KernelConfig cfg{
        &g_time,
        &g_hw,
        &g_events,
        &g_reset,
        &g_safety,
    };
    v3::kernel::init(cfg);

    // Sensing slice (#63): I2C adapter init + service start; the first slot
    // is due immediately (deadline now, within the kernel window).
    g_i2c.init();
    g_sensing.init(v3::sensing::SensingConfig{}, g_i2c);
    const std::uint64_t now = v3::monotonic::now_ms();
    v3::kernel::schedule(&sensing_tick, &g_sensing,
                         static_cast<std::uint32_t>(now));
}

void loop()
{
    v3::kernel::run(); // never returns (foreground WFI loop)
}
