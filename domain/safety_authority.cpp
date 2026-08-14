// Safety Authority implementation (design docs/safety-authority-design-v3.md
// sections 3.2, 3.3, 6; #45 §2-§5, #48 §2.2). Framework-free, host-deterministic.
// now приходит из glue; наблюдения - read-only через Sensing snapshots и CanPort.
#include "domain/safety_authority.h"

namespace v3
{
namespace safety
{
namespace
{

constexpr std::uint32_t kDiagMagic = 0x53414631u; // 'SAF1'

} // namespace

void SafetyAuthority::init(const Config& cfg, const sensing::SensingView* sensing,
                           v3::CanPort* can, v3::SafetyStateMarker* marker, v3::SafetyDiag* diag)
{
    m_cfg = cfg;
    m_sensing = sensing;
    m_can = can;
    m_marker = marker;
    m_diag = diag;
    m_health = SafetyHealth::Initializing;
    m_degraded_class = DegradedClass::None;
    m_fault = SafetyFault::None;
    m_deg_motion_ms = 0;
    m_last_tick_ms = 0;
    m_state_entry_ms = 0;
    m_seq = 0;
    m_timebase_set = false;
    m_funnel.reset();
    m_initialized = true;

    // Q5 A: маркер читается read-only (power-cycle != acknowledgment); crash-класс
    // latch в Фазе 1 невозможен (bumper/stall не подключены) - write_crash не вызывается.
    m_marker_pending = false;
    m_crash_count = 0;
    if (m_marker != nullptr)
    {
        const SafetyStateMarker::State s = m_marker->read_crash();
        m_marker_pending = s.crash_pending;
        m_crash_count = s.crash_count;
        if (m_marker_pending && m_events != nullptr)
        {
            m_events->crash_marker_pending(m_crash_count);
        }
    }

    if (m_diag != nullptr)
    {
        m_diag->magic = kDiagMagic;
        m_diag->version = 1u;
        m_diag->health = m_health;
        m_diag->degraded_class = m_degraded_class;
        m_diag->fault = m_fault;
    }
}

void SafetyAuthority::tick(std::uint64_t now)
{
    if (!m_initialized)
    {
        return;
    }
    if (!m_timebase_set)
    {
        m_last_tick_ms = now;
        m_started_ms = now;
        m_state_entry_ms = now;
        m_timebase_set = true;
    }

    // 1. Наблюдения (read-only, O(1)).
    const bool sensor_faulted = any_sensor_faulted();
    const bool directional_fresh = directional_sensing_fresh();
    const CanErrorState can_state = m_can != nullptr ? m_can->error_state() : CanErrorState::Active;

    // 2. Degraded-класс (#48 §2.2: T_deg - только motion-capable, is_motion_capable()).
    DegradedClass cls = DegradedClass::None;
    if (sensor_faulted || !directional_fresh)
    {
        cls = DegradedClass::Sensing; // HZ-05/06 (motion-capable)
    }
    else if (can_state != CanErrorState::Active)
    {
        cls = DegradedClass::CanBus; // HZ-03: транзиентен - шаг 4 латчит CanFailsafe в том же тике
    }

    // 3. FSM-переходы (Q2/Q5 A, #45 §2).
    switch (m_health)
    {
        case SafetyHealth::Initializing:
            if (m_marker_pending)
            {
                enter_fault(SafetyFault::CrashMarker, now);
            }
            else if (cls != DegradedClass::None)
            {
                enter_degraded(cls, now);
            }
            else if (ready_conditions(now))
            {
                enter_ready(now);
            }
            break;
        case SafetyHealth::Ready:
            if (cls != DegradedClass::None)
            {
                enter_degraded(cls, now);
            }
            break;
        case SafetyHealth::Degraded:
            if (cls == DegradedClass::None)
            {
                enter_ready(now); // квалифицированное снятие условия (streak, stationary)
            }
            else if (is_motion_capable(cls))
            {
                if (now >= m_last_tick_ms)
                {
                    m_deg_motion_ms += now - m_last_tick_ms; // monotonic clamp (NTP-immune, #43)
                }
                if (m_deg_motion_ms >= m_cfg.t_deg_ms)
                {
                    enter_fault(SafetyFault::DegradedTimeout, now);
                    emit_stop(StopProfile::Controlled); // Fault в покое -> CONTROLLED (#45 §4)
                }
            }
            break;
        case SafetyHealth::Fault:
            // Recovery (Q5 A, #45 §2/§5): auto-clear ТОЛЬКО DegradedTimeout (HZ-01/05/06);
            // CrashMarker (Q5 A) и CanFailsafe (HZ-03: явный reset после проверки шины) - нет.
            if (m_fault != SafetyFault::CrashMarker && m_fault != SafetyFault::CanFailsafe &&
                cls == DegradedClass::None && !motion_active())
            {
                const SafetyFault cleared = m_fault;
                m_fault = SafetyFault::None;     // latched fault = None вне Fault (§2.5)
                m_funnel.reset();                // release stop-intent (воронка-храповик):
                                                 //   иначе Ready с навсегда Stop/ForceStop -
                                                 //   движение невосстановимо (review MAJOR)
                m_health = SafetyHealth::Degraded; // ре-квалификация; T_deg-таймер рестартует
                m_degraded_class = DegradedClass::None;
                m_deg_motion_ms = 0;
                m_state_entry_ms = now;
                if (m_events != nullptr)
                {
                    m_events->health_changed(SafetyHealth::Fault, SafetyHealth::Degraded,
                                             m_degraded_class, cleared);
                }
            }
            break;
    }
    m_last_tick_ms = now;

    // 4. CAN fail-safe реакции (Q7.1 mitigation, INV-CAN-FAILSAFE; D5). Guard: уже
    // латченный CrashMarker не затирается (Q5 A); force-stop эмитится безусловно.
    if (can_state == CanErrorState::ErrorPassive)
    {
        if (m_health != SafetyHealth::Fault)
        {
            enter_fault(SafetyFault::CanFailsafe, now);
        }
        emit_stop(StopProfile::ForceStop); // строгий Q4-маппинг: потеря шины -> FORCE-STOP
        if (m_events != nullptr)
        {
            m_events->can_failsafe(can_state);
        }
    }
    if (can_state == CanErrorState::BusOff)
    {
        if (m_can != nullptr)
        {
            m_can->recover_bus_off(); // bounded re-integration (RM0090 §32.7); fault остаётся
        }
        if (m_health != SafetyHealth::Fault)
        {
            enter_fault(SafetyFault::CanFailsafe, now);
        }
        emit_stop(StopProfile::ForceStop); // Q4: потеря CAN-шины -> FORCE-STOP (INV-CAN-FAILSAFE)
        if (m_events != nullptr)
        {
            m_events->can_failsafe(can_state);
        }
    }

    // 5. Level 1 force-stop эмиссия (T_fs, #45 §4, INV-FORCE-STOP-CHANNEL): пока force-stop
    //    intent текущий - каждая граница шага, прямо из слота, вне FIFO. Frame-запись
    //    делает CAN-адаптер (force_stop_tx владеет кадром, kind=2); SA - счётчик.
    const Intent& cur = m_funnel.current();
    if (cur.kind == IntentKind::ForceStop || cur.stop_profile == StopProfile::ForceStop)
    {
        if (m_can != nullptr)
        {
            m_can->force_stop_tx();
            if (m_diag != nullptr)
            {
                ++m_diag->force_stops_issued;
            }
        }
    }

    update_diag(now);
}

const Intent& SafetyAuthority::arbitrate(const Intent& candidate)
{
    // INV-FAULT-ADMISSION / INV-SENSING-FRESH / Degraded-ограничение (item 4, Фаза 1: block):
    // движение допускается только в Ready (Ready подразумевает свежую направленную
    // сенсорику - иначе degraded-класс). Stop-intents проходят всегда (#45 §4).
    if (candidate.source == IntentSource::Activity && m_health != SafetyHealth::Ready)
    {
        if (m_diag != nullptr)
        {
            ++m_diag->activity_intents_rejected;
        }
        return m_funnel.current();
    }
    m_funnel.apply(candidate);
    return m_funnel.current();
}

void SafetyAuthority::enter_degraded(DegradedClass cls, std::uint64_t now)
{
    const SafetyHealth from = m_health;
    m_health = SafetyHealth::Degraded;
    m_degraded_class = cls;
    m_deg_motion_ms = 0; // таймер рестартует с входа в Degraded
    m_state_entry_ms = now;
    if (m_events != nullptr)
    {
        m_events->health_changed(from, SafetyHealth::Degraded, cls, SafetyFault::None);
    }
}

void SafetyAuthority::enter_fault(SafetyFault f, std::uint64_t now)
{
    const SafetyHealth from = m_health;
    m_health = SafetyHealth::Fault;
    m_fault = f;
    m_state_entry_ms = now;
    if (m_events != nullptr)
    {
        m_events->health_changed(from, SafetyHealth::Fault, m_degraded_class, f);
    }
}

void SafetyAuthority::enter_ready(std::uint64_t now)
{
    const SafetyHealth from = m_health;
    m_health = SafetyHealth::Ready;
    m_degraded_class = DegradedClass::None;
    m_deg_motion_ms = 0;
    m_state_entry_ms = now;
    if (m_events != nullptr)
    {
        m_events->health_changed(from, SafetyHealth::Ready, DegradedClass::None, SafetyFault::None);
    }
}

void SafetyAuthority::emit_stop(StopProfile profile)
{
    Intent it;
    it.kind = (profile == StopProfile::ForceStop) ? IntentKind::ForceStop : IntentKind::Stop;
    it.source = IntentSource::Safety;
    it.stop_profile = profile;
    it.seq = ++m_seq;
    m_funnel.apply(it);
    if (m_diag != nullptr)
    {
        ++m_diag->stops_issued;
    }
    if (m_events != nullptr)
    {
        m_events->stop_issued(profile, it.seq);
    }
}

bool SafetyAuthority::ready_conditions(std::uint64_t now) const
{
    // INV-STARTUP-GATE (#48 Q9, #45 §5): grace + requalification + reset-cause reconciled;
    // Ready <= 5 s - целевой бюджет, проверяется L4 (timestamps state_entry).
    if (!m_cfg.reset_cause_reconciled)
    {
        return false;
    }
    const std::uint64_t elapsed = now >= m_started_ms ? now - m_started_ms : 0; // clamp (NTP)
    if (elapsed < m_cfg.grace_ms)
    {
        return false;
    }
    // requalification: сенсорика здорова (уже проверено: cls == None в Ready-ветке), маркер
    // отсутствует (проверено в Initializing). Достаточно условий выше.
    return true;
}

bool SafetyAuthority::directional_sensing_fresh() const
{
    if (m_sensing == nullptr)
    {
        return false; // сенсорика недоступна - движение недопустимо (fail-safe default)
    }
    static constexpr sensing::SensorId kDirectional[2] = {sensing::SensorId::TofChannelReverse,
                                                          sensing::SensorId::TofChannelForward};
    for (const sensing::SensorId id : kDirectional)
    {
        sensing::SensorSnapshot snap;
        if (!m_sensing->get_snapshot(id, &snap))
        {
            return false;
        }
        // admission считает возраст + наличие образца, НЕ состояние сенсора (M4);
        // Faulted-состояние отдельно даёт degraded-класс (D4).
        if (!snap.has_sample)
        {
            return false;
        }
        const std::uint64_t age = snap.age_ms == 0xFFFFFFFFu ? UINT64_MAX : snap.age_ms;
        if (age >= m_cfg.t_fresh_directional_ms)
        {
            return false;
        }
    }
    return true;
}

bool SafetyAuthority::any_sensor_faulted() const
{
    if (m_sensing == nullptr)
    {
        return true; // сенсорика недоступна - degraded (fail-safe default)
    }
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(sensing::SensorId::Count); ++i)
    {
        sensing::SensorSnapshot snap;
        if (m_sensing->get_snapshot(static_cast<sensing::SensorId>(i), &snap) &&
            snap.state == sensing::HealthState::Faulted)
        {
            return true;
        }
    }
    return false;
}

void SafetyAuthority::update_diag(std::uint64_t now)
{
    if (m_diag == nullptr)
    {
        return;
    }
    m_diag->uptime_ms = now;
    m_diag->health = m_health;
    m_diag->degraded_class = m_degraded_class;
    m_diag->fault = m_fault;
    m_diag->state_entry_ms = m_state_entry_ms;
    m_diag->degraded_motion_ms = m_deg_motion_ms;
    m_diag->current_intent = m_funnel.current();
}

} // namespace safety
} // namespace v3
