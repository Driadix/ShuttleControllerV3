// Backup SRAM crash-marker adapter implementation (design
// docs/safety-authority-design-v3.md section 2.3; #45 Q5 A). One 32-bit word
// in an RTC backup register (RTC->BKP0R), preserved across system reset.
// Word-atomic store (Cortex-M4) - payload + CRC16 cannot be half-written.
#include "adapters/backup_marker.h"

#include <Arduino.h>
#include <cstdint>

#include "stm32f4xx_hal.h"

#include "domain/marker_format.h"

namespace v3
{

void BackupMarker::enable_backup_access()
{
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess(); // set PWR_CR.DBP: backup domain access
}

SafetyStateMarker::State BackupMarker::read_crash()
{
    enable_backup_access();
    const std::uint32_t word = RTC->BKP0R;
    const auto s = v3::safety::marker_decode(word);
    // init-штамп (Q5 A, design §2.3): CRC-fail (мусор первого boot BKP) трактуется
    // permissive «краха не было» и НЕМЕДЛЕННО перезаписывается валидным пустым
    // маркером - мусор никогда не читается повторно. Предикат - round-trip валидности
    // (marker_encode(decode(word)) != word => CRC-fail): валидные состояния (в т.ч.
    // {crash_pending=false, crash_count>0}) не затираются.
    if (v3::safety::marker_encode(s) != word)
    {
        RTC->BKP0R = v3::safety::marker_encode(v3::safety::MarkerState{});
    }
    return s;
}

void BackupMarker::write_crash(std::uint32_t crash_count)
{
    enable_backup_access();
    RTC->BKP0R = v3::safety::marker_encode(v3::safety::MarkerState{true, crash_count});
}

void BackupMarker::clear_crash()
{
    enable_backup_access();
    RTC->BKP0R = v3::safety::marker_encode(v3::safety::MarkerState{});
}

} // namespace v3
