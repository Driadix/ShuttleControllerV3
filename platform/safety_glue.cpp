// Composition-root glue implementation (see safety_glue.h). The kernel runs at
// most one bounded step per tick; the SafetySlot mechanism (#70 §2.5) calls the
// Safety Authority on every step boundary OUTSIDE the FIFO (T_check_jitter /
// T_arb <= 1 step at any backlog). Slot-duration measurement is NOT duplicated
// here - kernel process_tick already measures safety->tick (design §3.1, M3).
#include "platform/safety_glue.h"

#include "platform/actuation_schedule.h"

namespace v3
{
namespace safety
{
namespace
{

// Pinned at 0x20012000 by the firmware env linker flag (--section-start
// .bram_safety); stripped from the flash image by the runner. Runtime RAM only.
__attribute__((section(".bram_safety"))) SafetyDiag g_safety_diag;

} // namespace

SafetyDiag& safety_diag()
{
    return g_safety_diag;
}

void SafetySlotImpl::tick(std::uint64_t now)
{
    if (m_sa != nullptr)
    {
        m_sa->tick(now);
        // Re-arm эмиссионного шага при активации intent (тишина при idle, Q7.1 A).
        if (m_sa->intent_active() && !actuation_armed())
        {
            actuation_arm(now);
        }
    }
}

} // namespace safety
} // namespace v3
