// Reset-cause adapter implementation (target-only, HAL/CMSIS). STM32F405:
// RCC->CSR reset flags (IWDGRSTF, SFTRSTF, PINRSTF, PORRSTF, BORRSTF) are
// latched across resets until cleared by RMVF. Priority matches the fault
// model: watchdog (crash record) first, then software/external, then
// power-on/brownout (least diagnostic value at startup).
#include "adapters/reset_cause.h"

#ifdef ARDUINO

#include <Arduino.h>

namespace v3
{

ResetCause Stm32ResetCause::read()
{
    const std::uint32_t csr = RCC->CSR;

    // Clear the reset flags (RMVF) so the next reset cause latches fresh.
    RCC->CSR |= RCC_CSR_RMVF;

    if ((csr & RCC_CSR_IWDGRSTF) != 0)
    {
        return ResetCause::Watchdog;
    }
    if ((csr & RCC_CSR_SFTRSTF) != 0)
    {
        return ResetCause::Software;
    }
    if ((csr & RCC_CSR_PINRSTF) != 0)
    {
        return ResetCause::External;
    }
    if ((csr & (RCC_CSR_PORRSTF | RCC_CSR_BORRSTF)) != 0)
    {
        return ResetCause::PowerOn;
    }
    return ResetCause::Unknown;
}

} // namespace v3

#endif // ARDUINO
