// Arbitration funnel (issue #43 section 3.1): the single boundary through which
// all actuator intents pass. Out comes exactly one current intent; the Actuator
// Controller executes only that. Stop intents are never rejected.
#pragma once

#include "domain/intent.h"

namespace slice
{

class Arbitration
{
  public:
    // Passes `candidate` through the funnel. Returns the resulting current
    // intent. Safety intents always replace activity intents; stop intents are
    // never rejected (issue #45 section 4: stop-intents never deviate).
    Intent apply(Intent candidate)
    {
        // Stop intents are never rejected: any stop always replaces the current intent.
        if (candidate.kind == IntentKind::Stop || candidate.kind == IntentKind::ForceStop)
        {
            m_current = candidate;
            return m_current;
        }
        if (intent_preempts(candidate, m_current))
        {
            m_current = candidate;
        }
        return m_current;
    }

    // The single current intent; read-only for the Actuator Controller.
    const Intent& current() const { return m_current; }

    void reset() { m_current = Intent{}; }

  private:
    Intent m_current = {};
};

} // namespace slice
