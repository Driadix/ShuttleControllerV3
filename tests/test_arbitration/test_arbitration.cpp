// Arbitration funnel ordering (issue #43 section 3.1, safety model #45 section 4):
// total order SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT; stop intents are
// never rejected; the funnel emits exactly one current intent.
#include <gtest/gtest.h>

#include "domain/arbitration.h"
#include "domain/intent.h"

namespace
{

using slice::Arbitration;
using slice::Intent;
using slice::IntentKind;
using slice::IntentSource;
using slice::StopProfile;

TEST(Arbitration, ActivityIntentIsCurrentByDefault)
{
    Arbitration arb;
    Intent act{};
    act.kind = IntentKind::VelocitySetpoint;
    act.source = IntentSource::Activity;
    act.velocity = 100;

    const Intent out = arb.apply(act);
    EXPECT_EQ(out.source, IntentSource::Activity);
    EXPECT_EQ(out.velocity, 100);
}

TEST(Arbitration, SafetyStopReplacesActivity)
{
    Arbitration arb;
    Intent act{};
    act.kind = IntentKind::VelocitySetpoint;
    act.source = IntentSource::Activity;
    arb.apply(act);

    Intent stop{};
    stop.kind = IntentKind::Stop;
    stop.source = IntentSource::Safety;
    stop.stop_profile = StopProfile::Immediate;

    const Intent out = arb.apply(stop);
    EXPECT_EQ(out.kind, IntentKind::Stop);
    EXPECT_EQ(out.source, IntentSource::Safety);
}

TEST(Arbitration, ActivityCannotReplaceSafetyStop)
{
    Arbitration arb;
    Intent stop{};
    stop.kind = IntentKind::Stop;
    stop.source = IntentSource::Safety;
    arb.apply(stop);

    Intent act{};
    act.kind = IntentKind::VelocitySetpoint;
    act.source = IntentSource::Activity;
    act.velocity = 50;

    const Intent out = arb.apply(act);
    EXPECT_EQ(out.kind, IntentKind::Stop); // safety intent stays current
}

TEST(Arbitration, SafetyMotionCannotReplaceSafetyStop)
{
    Arbitration arb;
    Intent stop{};
    stop.kind = IntentKind::Stop;
    stop.source = IntentSource::Safety;
    stop.stop_profile = StopProfile::Controlled;
    arb.apply(stop);

    // Authorized bounded safety motion (evacuation / low-battery return,
    // safety model #45 section 4) must NOT replace an active safety stop:
    // documented order is SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT.
    Intent motion{};
    motion.kind = IntentKind::VelocitySetpoint;
    motion.source = IntentSource::Safety;

    const Intent out = arb.apply(motion);
    EXPECT_EQ(out.kind, IntentKind::Stop);
}

TEST(Arbitration, ForceStopNeverRejectedAndWinsOverStop)
{
    Arbitration arb;
    Intent stop{};
    stop.kind = IntentKind::Stop;
    stop.source = IntentSource::Safety;
    stop.stop_profile = StopProfile::Controlled;
    arb.apply(stop);

    Intent fs{};
    fs.kind = IntentKind::ForceStop;
    fs.source = IntentSource::Safety;

    const Intent out = arb.apply(fs);
    EXPECT_EQ(out.kind, IntentKind::ForceStop);
}

TEST(Arbitration, EqualRankActivityReplacesActivity)
{
    Arbitration arb;
    Intent a{};
    a.kind = IntentKind::VelocitySetpoint;
    a.source = IntentSource::Activity;
    a.velocity = 100;
    arb.apply(a);

    Intent b{};
    b.kind = IntentKind::VelocitySetpoint;
    b.source = IntentSource::Activity;
    b.velocity = 42;

    const Intent out = arb.apply(b);
    EXPECT_EQ(out.velocity, 42);
}

} // namespace
