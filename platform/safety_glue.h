// Composition-root glue for the Safety Authority slice (design
// docs/safety-authority-design-v3.md sections 3.1, 5.1; ticket #71).
// Host-buildable (no Arduino): wires the Safety Authority into the kernel
// SafetySlot (call on every step boundary, outside FIFO) and re-arms the
// actuation emission step when an intent becomes active (silence when idle).
//
// The pinned `.bram_safety` diagnostic instance lives here (linker section
// set by the firmware env flag --section-start=.bram_safety=0x20012000,
// stripped from the flash image by the runner - runtime RAM only); native
// build treats the section attribute as a plain global.
#pragma once

#include <cstdint>

#include "domain/diag_safety.h"
#include "domain/ports.h"
#include "domain/safety_authority.h"

namespace v3
{
namespace safety
{

// Pinned `.bram_safety` mirror (SafetyDiag). Single instance; SA / Actuator /
// CAN adapter get pointers to it in their init.
SafetyDiag& safety_diag();

// SafetySlot implementation (mechanism #70 §2.5): calls SafetyAuthority::tick
// on every step boundary, then arms the actuation step if an intent is active.
class SafetySlotImpl : public SafetySlot
{
  public:
    void bind(SafetyAuthority* sa) { m_sa = sa; }
    void tick(std::uint64_t now) override;

  private:
    SafetyAuthority* m_sa = nullptr;
};

} // namespace safety
} // namespace v3
