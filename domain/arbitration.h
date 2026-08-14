// Единственная arbitration-воронка (design docs/safety-authority-design-v3.md
// section 3.3; #43 §3.1, #45 §4). Production-форма slice::Arbitration (решение #85).
// Все actuator intents проходят через неё; наружу - единственный текущий intent.
// Framework-free, header-only, без аллокаций (R1).
#pragma once

#include "domain/safety_intent.h"

namespace v3
{
namespace safety
{

class Arbitration
{
  public:
    // Пропускает `candidate` через воронку; возвращает текущий intent.
    // - Stop-intents никогда не отклоняются (#45 §4): любой stop/force-stop заменяет
    //   motion-intent (текущий не-stop), но НЕ понижает уже установленный stop
    //   более высокого ранга (precedence SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT).
    // - Замена активного intent - только на границе bounded шага (#45 §4): воронка
    //   мутирует только внутри SafetySlot::tick (single-writer, foreground).
    const Intent& apply(const Intent& candidate)
    {
        const bool cand_stop = (candidate.kind == IntentKind::Stop ||
                                candidate.kind == IntentKind::ForceStop);
        const bool cur_stop = (m_current.kind == IntentKind::Stop ||
                               m_current.kind == IntentKind::ForceStop);
        if (cand_stop && !cur_stop)
        {
            m_current = candidate; // stop заменяет motion; никогда не отклоняется
            m_active = true;
            return m_current;
        }
        if (intent_preempts(candidate, m_current))
        {
            m_current = candidate;
            m_active = true;
        }
        return m_current;
    }

    // Единственный текущий intent (read-only для Actuator Controller, #43 §3.1).
    const Intent& current() const { return m_current; }

    // Был ли установлен хотя бы один intent (idle = false). Резерв для re-arm логики
    // эмиссионного шага (тишина на шине при отсутствии intent, Q7.1 A).
    bool active() const { return m_active; }

    void reset()
    {
        m_current = Intent{};
        m_active = false;
    }

  private:
    Intent m_current = {};
    bool m_active = false;
};

} // namespace safety
} // namespace v3
