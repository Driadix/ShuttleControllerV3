// Intents and stop profiles for the arbitration funnel (issue #43 section 3.1,
// safety model #45 section 4).
#pragma once

#include <cstdint>

namespace slice
{

enum class IntentSource : std::uint8_t
{
    Activity = 0, // Operation Runtime / Manual Session (auto + manual, one class)
    Safety = 1,   // Safety Authority
};

enum class IntentKind : std::uint8_t
{
    VelocitySetpoint = 0, // commanded speed (normal operation)
    Stop = 1,             // stop with a profile (CONTROLLED / IMMEDIATE)
    ForceStop = 2,        // force-stop: min extended ID frame, outside queues
    Lift = 3,             // lifter up/down (slice: synthetic actuator)
};

enum class StopProfile : std::uint8_t
{
    Controlled = 0, // ramp, bounded rate
    Immediate = 1,  // zero frame at next emission
    ForceStop = 2,  // extended min-ID frame + zero frame (bumper / CAN loss)
};

// Single current intent coming out of the funnel; Actuator Controller executes
// exactly this and holds no policy of its own (issue #43 section 3.1).
struct Intent
{
    IntentKind kind = IntentKind::VelocitySetpoint;
    IntentSource source = IntentSource::Activity;
    StopProfile stop_profile = StopProfile::Controlled;
    std::int16_t velocity = 0;   // scaled setpoint (synthetic domain units)
    std::uint32_t seq = 0;       // monotonic intent sequence, for trace IDs
};

// Total order enforced by the funnel: SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT.
// Returns true when `candidate` may replace `current`.
inline bool intent_preempts(const Intent& candidate, const Intent& current)
{
    const auto rank = [](const Intent& i) {
        if (i.source == IntentSource::Safety && i.kind == IntentKind::ForceStop)
        {
            return 3; // SAFETY_STOP (force-stop level)
        }
        if (i.source == IntentSource::Safety && i.kind == IntentKind::Stop)
        {
            return 2; // SAFETY_STOP (safety stop)
        }
        if (i.source == IntentSource::Safety)
        {
            return 2; // SAFETY_MOTION (authorized bounded safety motion)
        }
        return 1; // ACTIVITY_INTENT
    };
    return rank(candidate) >= rank(current);
}

} // namespace slice
