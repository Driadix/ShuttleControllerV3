// Identity adapter implementation (design docs/observability-design-v3.md
// section 4.2; ticket #72). STM32F405 96-bit UID at 0x1FFF7A10 (RM0090 §32.6).
#include "adapters/identity.h"

#ifdef ARDUINO

#include <Arduino.h>

namespace v3
{

std::uint32_t Stm32Identity::hardware_id() const
{
    // UID[0] is the high word of the 96-bit unique device identifier.
    return *reinterpret_cast<const std::uint32_t*>(0x1FFF7A10u);
}

} // namespace v3

#endif // ARDUINO
