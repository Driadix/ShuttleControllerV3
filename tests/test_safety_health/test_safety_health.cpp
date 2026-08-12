// Safety health model (issue #45 section 2, budgets #48 section 2):
// freshness loss under motion -> Degraded -> Fault after T_deg; qualified
// recovery restores Ready. C1a budget: sample age stays below T_fresh.
#include <gtest/gtest.h>

#include "domain/safety_health.h"

namespace
{

using slice::Health;
using slice::SafetyHealth;

TEST(SafetyHealth, StartsInitializingAndDeniesMotion)
{
    SafetyHealth h;
    EXPECT_EQ(h.health(), Health::Initializing);
    EXPECT_FALSE(h.motion_allowed());
}

TEST(SafetyHealth, FreshSampleKeepsReady)
{
    SafetyHealth h;
    h.set_ready();
    for (std::uint64_t now = 0; now < 10'000; now += 10)
    {
        h.tick(now, 10); // fresh sample
    }
    EXPECT_EQ(h.health(), Health::Ready);
    EXPECT_TRUE(h.motion_allowed());
}

TEST(SafetyHealth, StaleSampleDegradesThenFaultsAfterTdeg)
{
    SafetyHealth h;
    h.set_ready();

    // Freshness loss: age > T_fresh (300 ms) => Degraded at the next tick.
    h.tick(0, 400);
    EXPECT_EQ(h.health(), Health::Degraded);

    // Continuous Degraded for T_deg (60 s) => Fault (FAULT_DEGRADED_TIMEOUT class).
    h.tick(SafetyHealth::T_deg_ms - 1, 400);
    EXPECT_EQ(h.health(), Health::Degraded);
    h.tick(SafetyHealth::T_deg_ms, 400);
    EXPECT_EQ(h.health(), Health::Fault);
    EXPECT_FALSE(h.motion_allowed());
}

TEST(SafetyHealth, QualifiedRecoveryRestoresReadyBeforeTdeg)
{
    SafetyHealth h;
    h.set_ready();
    h.tick(0, 400); // Degraded
    EXPECT_EQ(h.health(), Health::Degraded);

    // Fresh sample inside the T_deg window: qualified recovery to Ready.
    h.tick(1000, 10);
    EXPECT_EQ(h.health(), Health::Ready);
}

TEST(SafetyHealth, FaultIsLatchedUntilExplicitReset)
{
    SafetyHealth h;
    h.set_ready();
    h.tick(0, 400);
    h.tick(SafetyHealth::T_deg_ms, 400);
    EXPECT_EQ(h.health(), Health::Fault);

    // Fresh samples must NOT auto-clear a latched fault.
    h.tick(SafetyHealth::T_deg_ms + 1000, 10);
    EXPECT_EQ(h.health(), Health::Fault);

    // Explicit reset -> requalify (Initializing), not straight to Ready.
    h.clear_fault();
    EXPECT_EQ(h.health(), Health::Initializing);
}

} // namespace
