// Health-ось и fault-классы Safety Authority (design docs/safety-authority-design-v3.md
// section 2.2; safety model #45 section 2, budgets #48 section 2.2). Framework-free.
#pragma once

#include <cstdint>

namespace v3
{
namespace safety
{

// Health-ось (#45 §2, Q2): движение разрешено только в Ready.
enum class SafetyHealth : std::uint8_t
{
    Initializing = 0, // стартап, движение запрещено (INV-STARTUP-GATE); grace-окно
    Ready = 1,        // движение разрешено; health-gates пройдены
    Degraded = 2,     // движение ограничено по capability-классам (item 4); motion-класс
                      //   blocked в Фазе 1; потолок <= 1.0 м/с (F5); T_deg = 60 s (#48 Q2)
    Fault = 3,        // latched fault; движение запрещено для operation/manual intents
};

// Degraded-классы (Q2 carve-outs, #48 §2.2): T_deg отсчитывают только motion-capable.
enum class DegradedClass : std::uint8_t
{
    None = 0,
    Sensing = 1,   // HZ-05/06: деградация сенсорики / I2C-шины (motion-capable)
    CanBus = 2,    // HZ-03: CAN error-state (транзиентен - error-passive латчит
                   //   CanFailsafe в том же тике; НЕ в перечне #48 §2.2, T_deg не идёт)
    Overtemp = 3,  // HZ-16: ATEMP (Фаза 2+, интерфейс резервируется; motion-capable)
    BmsStale = 4,  // HZ-17: информационный, T_deg НЕ идёт (F9, #48 §2.2 carve-out)
};

// Только motion-capable классы отсчитывают T_deg (#48 §2.2: HZ-05/06/16). Чистая функция.
inline bool is_motion_capable(DegradedClass cls)
{
    return cls == DegradedClass::Sensing || cls == DegradedClass::Overtemp;
}

// Latched fault классы (доменные; wire-маппинг - реестр #47, аддитивно).
enum class SafetyFault : std::uint8_t
{
    None = 0,
    DegradedTimeout = 1, // FAULT_DEGRADED_TIMEOUT (#48 Q2, #47); auto-clear (HZ-01/05/06, #45 §5)
    DirectionalToF = 2,  // directional ToF fault (HZ-01); резерв, Фаза 2+ (требует motion)
    CanFailsafe = 3,     // INV-CAN-FAILSAFE (Q7.1); НЕ auto-clear (HZ-03: явный reset после
                         //   проверки шины, #45)
    CrashMarker = 4,     // pending explicit-reset маркер (Q5 A); НЕ auto-clear
};

} // namespace safety
} // namespace v3
