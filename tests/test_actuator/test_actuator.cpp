// ActuatorController tests (design docs/safety-authority-design-v3.md section 7.3;
// T15-T18, T30). Emission gate 50 ms, zero-frame on stop, disarm, frame contract.
#include <gtest/gtest.h>

#include "domain/actuator.h"
#include "domain/safety_authority.h"
#include "tests/common/safety_fakes.h"

namespace
{

using v3::safety::ActuatorController;
using v3::safety::Intent;
using v3::safety::IntentKind;
using v3::safety::IntentSource;
using v3::safety::SafetyAuthority;
using v3::safety::SafetyHealth;
using v3::sensing::SensorId;

struct Harness
{
    testfakes::FakeSensingSnapshots sensing;
    testfakes::FakeCanPort can;
    testfakes::FakeMarker marker;
    v3::SafetyDiag diag;
    testfakes::FakeSafetyEvents events;
    SafetyAuthority sa;
    ActuatorController actuator;

    void init()
    {
        sa.set_events(&events);
        sa.init({}, &sensing, &can, &marker, &diag);
        actuator.init({}, sa, &can, &diag);
    }
    void boot() { sa.tick(0); } // первый тик: timebase

    // Ready + активный velocity intent (для emission-тестов).
    void to_ready_and_velocity()
    {
        boot();
        sa.tick(1000); // Ready
        Intent v;
        v.kind = IntentKind::VelocitySetpoint;
        v.source = IntentSource::Activity;
        v.velocity = 120;
        sa.arbitrate(v);
    }
};

// T15: Gate 50 ms: velocity intent эмитится раз в 50 ms, не чаще (control TX #48 §5).
TEST(Actuator, VelocityEmitsPerGate)
{
    Harness h;
    h.init();
    h.to_ready_and_velocity();

    h.actuator.step(2000); // первый кадр (now >= next_gate=0)
    EXPECT_EQ(h.can.tx_count(), 1u);

    h.actuator.step(2049); // < 50 ms с прошлого gate: без эмиссии
    EXPECT_EQ(h.can.tx_count(), 1u);

    h.actuator.step(2050); // ровно gate: кадр
    EXPECT_EQ(h.can.tx_count(), 2u);
}

// T16: Stop-эмиссия: нулевой кадр-компаньон каждый gate, пока stop intent текущий.
TEST(Actuator, StopEmitsZeroFramePerGate)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100);
    h.sa.tick(1100 + 60'000); // Fault (DegradedTimeout) -> stop CONTROLLED

    h.actuator.step(1100 + 61'000); // нулевой кадр
    EXPECT_EQ(h.can.tx_count(), 1u);
    EXPECT_EQ(h.can.tx_frame(0).id, v3::safety::kCanTractionId);
    EXPECT_EQ(h.can.tx_frame(0).len, 8u);
    EXPECT_EQ(h.can.tx_frame(0).data[1], 0u); // velocity 0

    h.actuator.step(1100 + 61'050); // ещё gate - снова нулевой кадр (непрерывно)
    EXPECT_EQ(h.can.tx_count(), 2u);
}

// T17: Disarm: нет активного intent -> шаг не эмитит (тишина на шине, Q7.1 A).
TEST(Actuator, DisarmWhenNoIntent)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready, но intent не установлен
    h.actuator.step(2000);
    h.actuator.step(3000);
    EXPECT_EQ(h.can.tx_count(), 0u);
}

// T18: build_actuator_frame по контракту §4.3: velocity -> 0x100, lift -> 0x101.
TEST(Actuator, FrameContract)
{
    Intent v;
    v.kind = IntentKind::VelocitySetpoint;
    v.velocity = -300;
    const v3::CanFrame fv = v3::safety::build_actuator_frame(v);
    EXPECT_EQ(fv.id, 0x100u);
    EXPECT_EQ(fv.len, 8u);
    EXPECT_EQ(fv.data[0], 0u); // направление (v < 0)
    EXPECT_EQ(fv.data[1], static_cast<std::uint8_t>((300u >> 8) & 0xFFu));
    EXPECT_EQ(fv.data[2], static_cast<std::uint8_t>(300u & 0xFFu));

    Intent l;
    l.kind = IntentKind::Lift;
    l.velocity = 40;
    const v3::CanFrame fl = v3::safety::build_actuator_frame(l);
    EXPECT_EQ(fl.id, 0x101u);
    EXPECT_EQ(fl.data[2], 40u);
}

// T30: T_eso stop-пути: от trigger (safety-граница) до нулевого кадра <= T_arb + T_emit
// (10 + 50 ms) <= T_eso 70 ms (#48 §2). Детерминированно по инъекции времени.
// Путь: ErrorPassive -> stop FORCE-STOP в том же тике (воронка) -> нулевой кадр-компаньон
// из actuator по gate <= 50 ms.
TEST(Actuator, StopEmissionWithinTeso)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready

    const std::uint64_t t0 = 2000;
    h.can.set_error_state(v3::CanErrorState::ErrorPassive);
    h.sa.tick(t0); // Fault (CanFailsafe) + force-stop intent (T_arb в том же тике)

    // Force-stop кадр - из SA-слота (T_fs, проверено T20); здесь - companion zero-кадр
    // в пределах T_emit (<= 50 ms gate): сумма <= 60 ms <= T_eso 70 ms.
    h.actuator.step(t0 + 50);
    ASSERT_GE(h.can.tx_count(), 1u);
    const v3::CanFrame& f = h.can.tx_frame(0);
    EXPECT_EQ(f.id, v3::safety::kCanTractionId);
    EXPECT_EQ(f.data[0], 0u); // нулевой кадр (velocity 0)
    EXPECT_EQ(f.data[1], 0u);
    EXPECT_EQ(f.data[2], 0u);
}

} // namespace
