// CAN port contract tests (design docs/safety-authority-design-v3.md section 7.3;
// T19, T21, T29). The real bxCAN adapter is target-only (Arduino); host tests verify
// the PORT CONTRACT via the fake and its integration with Safety Authority /
// ActuatorController. Error-state transitions and RX-overflow policy (#13).
#include <gtest/gtest.h>

#include "domain/actuator.h"
#include "domain/safety_authority.h"
#include "tests/common/safety_fakes.h"

namespace
{

using v3::safety::Intent;
using v3::safety::IntentKind;
using v3::safety::IntentSource;
using v3::safety::SafetyAuthority;
using v3::safety::SafetyFault;
using v3::safety::SafetyHealth;

// T19: Бюджет TX: при исчерпании бюджета тика tx=false + drop учтён адаптером
// (can_tx_dropped); actuator не пишет frame record для отклонённого кадра.
TEST(CanPort, TxBudgetExhausted)
{
    testfakes::FakeCanPort can;
    can.set_tx_budget(0u); // бюджет исчерпан сразу

    testfakes::FakeSensingSnapshots sensing;
    testfakes::FakeMarker marker;
    v3::SafetyDiag diag;
    testfakes::FakeSafetyEvents events;
    SafetyAuthority sa;
    v3::safety::ActuatorController actuator;

    sa.set_events(&events);
    sa.init({}, &sensing, &can, &marker, &diag);
    actuator.init({}, sa, &can, &diag);

    sa.tick(0);  // первый тик: timebase
    sa.tick(1000); // Ready
    Intent v;
    v.kind = IntentKind::VelocitySetpoint;
    v.source = IntentSource::Activity;
    v.velocity = 50;
    sa.arbitrate(v);

    actuator.step(2000);
    EXPECT_EQ(can.tx_count(), 0u); // ни один кадр не отправлен (бюджет 0)
    EXPECT_GT(can.dropped(), 0u);  // drop учтён адаптером (#48 §7)
    // frame record для отклонённого кадра НЕ пишется (отправлен не был).
    EXPECT_EQ(diag.frame_head, 0u);
}

// T21: SA-реакция на error-state переходы: ErrorPassive -> fault + FORCE-STOP;
// последующий BusOff -> recover_bus_off (bounded re-integration), fault остаётся.
TEST(CanPort, ErrorStateReactions)
{
    testfakes::FakeSensingSnapshots sensing;
    testfakes::FakeCanPort can;
    testfakes::FakeMarker marker;
    v3::SafetyDiag diag;
    testfakes::FakeSafetyEvents events;
    SafetyAuthority sa;
    sa.set_events(&events);
    sa.init({}, &sensing, &can, &marker, &diag);

    sa.tick(1000); // Ready
    can.set_error_state(v3::CanErrorState::ErrorPassive);
    sa.tick(1100);
    EXPECT_EQ(sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(sa.fault(), SafetyFault::CanFailsafe);
    EXPECT_EQ(events.stop_count(), 1u);

    can.set_error_state(v3::CanErrorState::BusOff);
    sa.tick(1200);
    EXPECT_EQ(can.recover_count(), 1u); // bounded re-integration вызван
    EXPECT_EQ(sa.fault(), SafetyFault::CanFailsafe);
}

// T29: RX-drain бюджет (<= 64/тик) + overflow drop (#13, #43 §4): drain отдаёт
// до budget кадров, переполнение -> drop + счётчик.
TEST(CanPort, RxDrainBudgetAndOverflow)
{
    testfakes::FakeCanPort can;
    constexpr std::uint32_t kBudget = 64u;
    for (std::uint32_t i = 0; i < kBudget + 1; ++i)
    {
        v3::CanFrame f;
        f.id = 0x100u + i;
        can.push_rx(f); // 65 кадров в RX
    }
    v3::CanFrame out[64] = {};
    const std::uint32_t drained = can.rx_drain(out, kBudget);
    EXPECT_EQ(drained, kBudget);        // <= budget
    EXPECT_EQ(can.rx_dropped(), 1u);    // переполнение: drop + счётчик (#43 §4)
    EXPECT_EQ(out[0].id, 0x100u);
}

} // namespace
