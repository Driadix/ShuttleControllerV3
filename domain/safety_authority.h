// Safety Authority: единая safety-политика (design
// docs/safety-authority-design-v3.md sections 3, 5.2; #43 §2, #45 §2-§5).
// framework-free: now приходит из glue, наблюдения - через порты. Владелец
// единственного экземпляра arbitration-воронки. Реализация обязательной
// safety-границы SafetySlot (#70 §2.5): tick на каждой границе шага, вне FIFO.
#pragma once

#include <cstdint>

#include "domain/arbitration.h"
#include "domain/diag_safety.h"
#include "domain/sensing.h"

namespace v3
{
namespace safety
{

class SafetyAuthority
{
  public:
    struct Config
    {
        std::uint32_t grace_ms = 1000;            // startup grace (#48 §9)
        std::uint32_t t_deg_ms = 60'000;          // непрерывный motion-capable Degraded => Fault (#48 Q2)
        std::uint32_t t_fresh_directional_ms = 300; // T_fresh ToF (#45 O3, pre-allocated #43)
        bool reset_cause_reconciled = true;       // reconciliation на стартапе (glue передаёт)
    };

    // Типизированные события (sink - KernelEvents-заглушка Ф1 / Observability #72 Ф2;
    // эмиссия у SA, счётчики у Producer - #43 §4). Аддитивный контракт к #70.
    struct Events
    {
        virtual void health_changed(SafetyHealth from, SafetyHealth to,
                                    DegradedClass cls, SafetyFault fault) = 0;
        virtual void stop_issued(StopProfile profile, std::uint32_t seq) = 0;
        virtual void can_failsafe(CanErrorState state) = 0;
        virtual void crash_marker_pending(std::uint32_t crash_count) = 0;
    };

    // Стартап (foreground, после kernel::init): сброс FSM, чтение маркера (Q5 A,
    // read-only), установка портов. diag - указатель на pinned `.bram_safety`
    // зеркало (§2.5). Не вызывает Arduino.
    void init(const Config& cfg, const sensing::SensingView* sensing,
              v3::CanPort* can, v3::SafetyStateMarker* marker, v3::SafetyDiag* diag);

    void set_events(Events* events) { m_events = events; }

    // Обязательная safety-граница: вызывается kernel'ом через SafetySlot::tick на КАЖДОЙ
    // границе шага, вне FIFO (механизм #70 §2.5). Bounded (<= T_step), foreground-only.
    void tick(std::uint64_t now);

    // Единственная arbitration-воронка (#43 §3.1). Вызывается производителями activity
    // intents (Operation Runtime #74, Manual Session #77) на границе их bounded шага и
    // Safety Authority со своими safety-intents. Stop-intents никогда не отклоняются (#45 §4).
    const Intent& arbitrate(const Intent& candidate);

    // Текущий intent из воронки (read-only для Actuator Controller).
    const Intent& current_intent() const { return m_funnel.current(); }
    // Есть активный (не-idle) intent -> вооружать эмиссионный шаг.
    bool intent_active() const { return m_funnel.active(); }

    SafetyHealth health() const { return m_health; }
    SafetyFault fault() const { return m_fault; }
    DegradedClass degraded_class() const { return m_degraded_class; }
    bool motion_allowed() const { return m_health == SafetyHealth::Ready; }

  private:
    void enter_degraded(DegradedClass cls, std::uint64_t now);
    void enter_fault(SafetyFault f, std::uint64_t now);
    void enter_ready(std::uint64_t now);
    void emit_stop(StopProfile profile);
    bool ready_conditions(std::uint64_t now) const;
    bool motion_active() const { return intent_motion(m_funnel.current()); }
    bool directional_sensing_fresh() const;
    bool any_sensor_faulted() const;
    void update_diag(std::uint64_t now);

    Config m_cfg{};
    const sensing::SensingView* m_sensing = nullptr;
    v3::CanPort* m_can = nullptr;
    v3::SafetyStateMarker* m_marker = nullptr;
    v3::SafetyDiag* m_diag = nullptr;
    Events* m_events = nullptr;
    Arbitration m_funnel;
    SafetyHealth m_health = SafetyHealth::Initializing;
    DegradedClass m_degraded_class = DegradedClass::None;
    SafetyFault m_fault = SafetyFault::None;
    bool m_marker_pending = false;
    std::uint32_t m_crash_count = 0;
    std::uint64_t m_deg_motion_ms = 0;
    std::uint64_t m_last_tick_ms = 0;
    std::uint64_t m_state_entry_ms = 0;
    std::uint64_t m_started_ms = 0;
    std::uint32_t m_seq = 0;
    bool m_timebase_set = false;
    bool m_initialized = false;
};

} // namespace safety
} // namespace v3
