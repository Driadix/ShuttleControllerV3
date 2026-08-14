// Эмиссионный каркас Actuator Controller (design docs/safety-authority-design-v3.md
// sections 3.4, 5.2; #43 §4: единственный производитель кадров 100/101). Потребляет
// current_intent из воронки (read-only), эмитит кадры по gate 50 ms (#48 §5 control TX);
// stop-intents - нулевой кадр-компаньон каждый gate; force-stop НЕ здесь (Level 1,
// из SafetySlot::tick, §3.2/§3.5). Без ramp/lifter-политики (drive algorithms -
// capability-слайсы). Framework-free, bounded step.
#pragma once

#include <cstdint>

#include "domain/can_contract.h"
#include "domain/diag_safety.h"
#include "domain/safety_authority.h"

namespace v3
{
namespace safety
{

class ActuatorController
{
  public:
    struct Config
    {
        std::uint32_t tx_gate_ms = 50; // control TX каденция (#48 §5)
    };

    // diag - `.bram_safety` зеркало (frame records, §2.5).
    void init(const Config& cfg, const SafetyAuthority& sa, v3::CanPort* can, v3::SafetyDiag* diag);

    // Bounded шаг (composition root): эмитит текущий intent из воронки по gate 50 ms;
    // stop-intents - нулевой кадр-компаньон каждый gate; force-stop НЕ здесь. Не блокирует
    // дольше T_step.
    void step(std::uint64_t now);

    // Активен ли intent (для re-arm/disarm логики glue: idle = тишина на шине, Q7.1 A).
    bool active() const { return m_sa != nullptr && m_sa->intent_active(); }

  private:
    Config m_cfg{};
    const SafetyAuthority* m_sa = nullptr;
    v3::CanPort* m_can = nullptr;
    v3::SafetyDiag* m_diag = nullptr;
    std::uint64_t m_next_gate_ms = 0;
};

} // namespace safety
} // namespace v3
