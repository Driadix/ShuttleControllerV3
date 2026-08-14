// Exclusive Control Activity slot implementation (design
// docs/operation-runtime-design-v3.md section 2.6; #46 I-LC-4).
#include "domain/slot.h"

namespace v3
{
namespace slot
{

bool ExclusiveSlot::try_claim(Activity a)
{
    if (a == Activity::Idle)
    {
        return false; // Idle is not claimable
    }
    if (m_current != Activity::Idle)
    {
        return false;
    }
    m_current = a;
    return true;
}

void ExclusiveSlot::release(Activity a)
{
    if (m_current == a)
    {
        m_current = Activity::Idle;
    }
    // mismatch: no-op (single-writer invariant - only the owner releases)
}

} // namespace slot
} // namespace v3
