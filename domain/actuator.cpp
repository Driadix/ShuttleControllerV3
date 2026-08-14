// ActuatorController implementation (design docs/safety-authority-design-v3.md
// section 3.4; #43 §4, #48 §5). Emits the arbitrated intent as 100/101 or the
// companion zero frame per the 50 ms control-TX gate. Force-stop is NOT emitted
// here - it is the Level 1 path (Safety Authority -> mailbox from SafetySlot::tick).
// The frame record (kind 0=100/101, 1=zero) is written to `.bram_safety`; CAN
// TX budget/statistics live in the adapter (can_tx_count/can_tx_dropped).
#include "domain/actuator.h"

namespace v3
{
namespace safety
{

void ActuatorController::init(const Config& cfg, const SafetyAuthority& sa,
                              v3::CanPort* can, v3::SafetyDiag* diag)
{
    m_cfg = cfg;
    m_sa = &sa;
    m_can = can;
    m_diag = diag;
    m_next_gate_ms = 0;
}

void ActuatorController::step(std::uint64_t now)
{
    if (m_sa == nullptr)
    {
        return;
    }
    if (!m_sa->intent_active())
    {
        return; // disarm: idle = тишина на шине (fail-safe приводов, Q7.1 A)
    }
    const Intent& it = m_sa->current_intent();
    const bool stopping = (it.kind == IntentKind::Stop || it.kind == IntentKind::ForceStop);
    if (now < m_next_gate_ms)
    {
        return;
    }
    m_next_gate_ms = now + m_cfg.tx_gate_ms;
    const CanFrame f = stopping ? zero_frame() : build_actuator_frame(it);
    if (m_can != nullptr)
    {
        if (m_can->tx(f)) // бюджет/статистика - адаптер (can_tx_count/can_tx_dropped)
        {
            if (m_diag != nullptr)
            {
                m_diag->record_frame(stopping ? 1u : 0u, f, now);
            }
        }
    }
}

} // namespace safety
} // namespace v3
