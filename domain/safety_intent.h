// Intents и stop-профили arbitration-воронки (design
// docs/safety-authority-design-v3.md section 2.1; #43 §3.1, #45 §4).
// Production-форма slice::intent.h (решение #85: структуры эволюционируют).
// Framework-free, fixed-width (R3), без аллокаций (R1).
#pragma once

#include <cstdint>

namespace v3
{
namespace safety
{

enum class IntentSource : std::uint8_t
{
    Activity = 0, // Operation Runtime / Manual Session (auto + manual - один класс, #45 §4)
    Safety = 1,   // Safety Authority
};

enum class IntentKind : std::uint8_t
{
    VelocitySetpoint = 0, // commanded speed (нормальная работа)
    Stop = 1,             // stop с профилем (CONTROLLED / IMMEDIATE)
    ForceStop = 2,        // force-stop: min extended ID кадр, вне очередей (#43 §4)
    Lift = 3,             // лифтер вверх/вниз (зарезервирован; лифтер-контракты - #47/Фаза 3)
};

enum class StopProfile : std::uint8_t
{
    Controlled = 0, // ramp, bounded rate (ramp-алгоритм - вне слайса; Фаза 1: нулевой кадр)
    Immediate = 1,  // нулевой кадр в следующую эмиссию
    ForceStop = 2,  // extended min-ID кадр + нулевой кадр (#45 §4)
};

// Единственный текущий intent из воронки; Actuator Controller исполняет только его
// и не имеет собственной policy (#43 §3.1).
struct Intent
{
    IntentKind kind = IntentKind::VelocitySetpoint;
    IntentSource source = IntentSource::Activity;
    StopProfile stop_profile = StopProfile::Controlled;
    std::int16_t velocity = 0;   // масштабированный setpoint (доменные единицы; контракт кадра)
    std::uint32_t seq = 0;       // монотонный номер intent (trace IDs, .bram_safety)
};

// Тотальный порядок воронки (SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT, #45 §4).
// Чистая функция (host-тестируема). Среди stop-интентов ранг выше - перебивает.
inline bool intent_preempts(const Intent& candidate, const Intent& current)
{
    const auto rank = [](const Intent& i) {
        if (i.source == IntentSource::Safety && i.kind == IntentKind::ForceStop)
        {
            return 4; // SAFETY_STOP (force-stop, наивысший)
        }
        if (i.source == IntentSource::Safety && i.kind == IntentKind::Stop)
        {
            return 3; // SAFETY_STOP (safety stop)
        }
        if (i.source == IntentSource::Safety)
        {
            return 2; // SAFETY_MOTION (авторизованное bounded safety движение)
        }
        return 1; // ACTIVITY_INTENT
    };
    return rank(candidate) > rank(current) ||
           (rank(candidate) == rank(current) && candidate.kind == current.kind);
}

// Является ли intent motion (для stationary-предиката recovery и admission).
inline bool intent_motion(const Intent& i)
{
    return i.kind == IntentKind::VelocitySetpoint || i.kind == IntentKind::Lift;
}

} // namespace safety
} // namespace v3
