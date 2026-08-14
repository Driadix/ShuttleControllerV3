// Arbitration-воронка и intent_preempts (design docs/safety-authority-design-v3.md
// sections 2.1, 3.3; #43 §3.1, #45 §4). Production-форма v3::safety.
#include <gtest/gtest.h>

#include "domain/arbitration.h"
#include "domain/safety_intent.h"

namespace
{

using v3::safety::Arbitration;
using v3::safety::Intent;
using v3::safety::IntentKind;
using v3::safety::IntentSource;
using v3::safety::StopProfile;

Intent velocity(std::int16_t v)
{
    Intent i;
    i.kind = IntentKind::VelocitySetpoint;
    i.source = IntentSource::Activity;
    i.velocity = v;
    return i;
}

Intent safety_stop(StopProfile p)
{
    Intent i;
    i.kind = IntentKind::Stop;
    i.source = IntentSource::Safety;
    i.stop_profile = p;
    return i;
}

Intent safety_force()
{
    Intent i;
    i.kind = IntentKind::ForceStop;
    i.source = IntentSource::Safety;
    return i;
}

TEST(Arbitration, EmptyFunnelIsIdle)
{
    Arbitration a;
    EXPECT_FALSE(a.active());
    EXPECT_EQ(a.current().seq, 0u);
}

TEST(Arbitration, ActivityIntentBecomesCurrent)
{
    Arbitration a;
    const Intent& cur = a.apply(velocity(100));
    EXPECT_TRUE(a.active());
    EXPECT_EQ(cur.velocity, 100);
    EXPECT_EQ(cur.source, IntentSource::Activity);
}

// T11: stop-intents никогда не отклоняются (#45 §4): stop/force-stop заменяют motion.
TEST(Arbitration, StopReplacesMotionNeverRejected)
{
    Arbitration a;
    (void)a.apply(velocity(100));
    const Intent& s = a.apply(safety_stop(StopProfile::Immediate));
    EXPECT_EQ(s.kind, IntentKind::Stop);
    EXPECT_EQ(s.stop_profile, StopProfile::Immediate);

    // Force-stop тоже заменяет motion.
    Arbitration a2;
    (void)a2.apply(velocity(50));
    const Intent& fs = a2.apply(safety_force());
    EXPECT_EQ(fs.kind, IntentKind::ForceStop);
}

// T12: тотальный порядок SAFETY_STOP > SAFETY_MOTION > ACTIVITY_INTENT.
TEST(Arbitration, TotalOrderPrecedence)
{
    // SAFETY_MOTION (safety velocity) перебивает ACTIVITY velocity.
    Arbitration a;
    (void)a.apply(velocity(100));
    Intent sv;
    sv.kind = IntentKind::VelocitySetpoint;
    sv.source = IntentSource::Safety;
    const Intent& m = a.apply(sv);
    EXPECT_EQ(m.source, IntentSource::Safety);

    // SAFETY_STOP перебивает SAFETY_MOTION.
    const Intent& st = a.apply(safety_stop(StopProfile::Controlled));
    EXPECT_EQ(st.kind, IntentKind::Stop);

    // Среди stop-интентов: safety stop не понижается activity stop.
    Arbitration a2;
    (void)a2.apply(velocity(10));
    (void)a2.apply(safety_stop(StopProfile::Controlled));
    Intent activity_stop;
    activity_stop.kind = IntentKind::Stop;
    activity_stop.source = IntentSource::Activity;
    const Intent& a2cur = a2.apply(activity_stop);
    EXPECT_EQ(a2cur.source, IntentSource::Safety); // safety stop остаётся (rank выше)
}

// Force-stop не понижается никаким stop/motion.
TEST(Arbitration, ForceStopIsHighest)
{
    Arbitration a;
    (void)a.apply(velocity(10));
    (void)a.apply(safety_force());
    const Intent& after = a.apply(safety_stop(StopProfile::Immediate));
    EXPECT_EQ(after.kind, IntentKind::ForceStop);
}

TEST(Arbitration, ResetClears)
{
    Arbitration a;
    (void)a.apply(velocity(5));
    a.reset();
    EXPECT_FALSE(a.active());
    EXPECT_EQ(a.current().seq, 0u);
}

// intent_preempts: property по всем классам.
TEST(IntentPreempts, RankProperty)
{
    const Intent activity_v = velocity(1);
    const Intent safety_v = [] { Intent i; i.source = IntentSource::Safety; i.kind = IntentKind::VelocitySetpoint; return i; }();
    const Intent safety_st = safety_stop(StopProfile::Controlled);
    const Intent safety_fs = safety_force();

    EXPECT_FALSE(v3::safety::intent_preempts(activity_v, safety_v)); // activity < safety motion
    EXPECT_TRUE(v3::safety::intent_preempts(safety_v, activity_v));
    EXPECT_TRUE(v3::safety::intent_preempts(safety_st, safety_v));
    EXPECT_TRUE(v3::safety::intent_preempts(safety_fs, safety_st));
    EXPECT_FALSE(v3::safety::intent_preempts(safety_fs, safety_fs) == false); // same rank+kind -> true
}

TEST(IntentMotion, DetectsMotionKinds)
{
    EXPECT_TRUE(v3::safety::intent_motion(velocity(1)));
    EXPECT_FALSE(v3::safety::intent_motion(safety_stop(StopProfile::Controlled)));
}

} // namespace
