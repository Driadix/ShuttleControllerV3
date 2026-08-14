// Safety Authority tests (design docs/safety-authority-design-v3.md section 7.3;
// T1-T10, T20, T22, T23, T26-T28, T31). Health FSM, admission (INV-SENSING-FRESH /
// INV-FAULT-ADMISSION), arbitration integration, CAN fail-safe, crash marker, recovery.
#include <gtest/gtest.h>

#include "domain/safety_authority.h"
#include "tests/common/safety_fakes.h"

namespace
{

using v3::safety::DegradedClass;
using v3::safety::Intent;
using v3::safety::IntentKind;
using v3::safety::IntentSource;
using v3::safety::SafetyAuthority;
using v3::safety::SafetyFault;
using v3::safety::SafetyHealth;
using v3::safety::StopProfile;
using v3::sensing::HealthState;
using v3::sensing::SensorId;

struct Harness
{
    testfakes::FakeSensingSnapshots sensing;
    testfakes::FakeCanPort can;
    testfakes::FakeMarker marker;
    v3::SafetyDiag diag;
    testfakes::FakeSafetyEvents events;
    SafetyAuthority sa;

    void init()
    {
        // events перед init: crash_marker_pending долетает при pending-маркере (T8).
        sa.set_events(&events);
        sa.init({}, &sensing, &can, &marker, &diag);
    }
    void init(const SafetyAuthority::Config& cfg)
    {
        sa.set_events(&events);
        sa.init(cfg, &sensing, &can, &marker, &diag);
    }
    // Первый тик: устанавливает timebase (started_ms). Затем tick(1000) -> Ready
    // (grace 1 s, #48 §9). Для тестов, требующих Ready.
    void boot() { sa.tick(0); }
};

Intent activity_velocity()
{
    Intent i;
    i.kind = IntentKind::VelocitySetpoint;
    i.source = IntentSource::Activity;
    i.seq = 1;
    return i;
}

// T1: Стартап Initializing -> Ready после grace (1 s) при здоровой сенсорике;
// движение до Ready запрещено (admission).
TEST(SafetyAuthority, StartupToReadyAfterGrace)
{
    Harness h;
    h.init();
    h.boot(); // первый тик: timebase (started_ms = 0)

    EXPECT_EQ(h.sa.health(), SafetyHealth::Initializing);
    EXPECT_FALSE(h.sa.motion_allowed());

    // Движение до Ready отклоняется (INV-STARTUP-GATE).
    h.sa.arbitrate(activity_velocity());
    EXPECT_EQ(h.diag.activity_intents_rejected, 1u);
    EXPECT_FALSE(h.sa.intent_active());

    // grace (1 s) ещё не прошёл.
    h.sa.tick(500);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Initializing);

    // По истечении grace -> Ready (Ready <= 5 s, #48 Q7).
    h.sa.tick(1000);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Ready);
    EXPECT_TRUE(h.sa.motion_allowed());
    EXPECT_EQ(h.events.health_count(), 1u);
    EXPECT_EQ(h.events.health(0).to, SafetyHealth::Ready);
}

// T2: Degraded-вход: directional ToF Faulted -> Degraded (Sensing) + событие.
TEST(SafetyAuthority, DirectionalFaultDegrades)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready

    h.sensing.set_faulted(SensorId::TofChannelForward); // CH_F (0x0A)
    h.sa.tick(1100);

    EXPECT_EQ(h.sa.health(), SafetyHealth::Degraded);
    EXPECT_EQ(h.sa.degraded_class(), DegradedClass::Sensing);
    EXPECT_EQ(h.events.health(h.events.health_count() - 1).to, SafetyHealth::Degraded);
}

// T3: T_deg: непрерывный Degraded (Sensing) 60 s -> Fault + FAULT_DEGRADED_TIMEOUT +
// stop-intent CONTROLLED (Fault в покое, #45 §4).
TEST(SafetyAuthority, DegradedTimeoutFaultsAfterTdeg)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100); // Degraded

    h.sa.tick(1100 + 59'000); // 59 s: ещё Degraded
    EXPECT_EQ(h.sa.health(), SafetyHealth::Degraded);

    h.sa.tick(1100 + 60'000); // 60 s: Fault
    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(h.sa.fault(), SafetyFault::DegradedTimeout);
    EXPECT_FALSE(h.sa.motion_allowed());
    EXPECT_EQ(h.events.stop_count(), 1u);
    EXPECT_EQ(h.events.last_profile(), StopProfile::Controlled);
}

// T4: carve-out #48 §2.2: is_motion_capable по всем классам.
TEST(SafetyAuthority, MotionCapableCarveOut)
{
    EXPECT_TRUE(v3::safety::is_motion_capable(DegradedClass::Sensing));
    EXPECT_TRUE(v3::safety::is_motion_capable(DegradedClass::Overtemp));
    EXPECT_FALSE(v3::safety::is_motion_capable(DegradedClass::CanBus));
    EXPECT_FALSE(v3::safety::is_motion_capable(DegradedClass::BmsStale));
    EXPECT_FALSE(v3::safety::is_motion_capable(DegradedClass::None));
}

// T5: Qualified recovery: Degraded-условие снято -> Ready; T_deg-счётчик сброшен.
TEST(SafetyAuthority, DegradedRecoversToReady)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100); // Degraded
    h.sa.tick(1100 + 30'000); // 30 s в Degraded (таймер идёт)

    h.sensing.set_fresh_all(0); // условие снято (sensors healthy + fresh)
    h.sa.tick(1100 + 31'000); // -> Ready

    EXPECT_EQ(h.sa.health(), SafetyHealth::Ready);
    EXPECT_EQ(h.diag.degraded_motion_ms, 0u); // таймер сброшен
}

// T6: INV-SENSING-FRESH: motion admission требует свежую направленную сенсорику
// (в Degraded activity intent отклоняется).
TEST(SafetyAuthority, FreshnessAdmissionRejectsInDegraded)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100); // Degraded

    h.sa.arbitrate(activity_velocity());
    EXPECT_EQ(h.diag.activity_intents_rejected, 1u);
    EXPECT_FALSE(h.sa.intent_active()); // воронка осталась пустой
}

// T7: INV-FAULT-ADMISSION: в Fault activity отклоняется; safety stop проходит.
TEST(SafetyAuthority, FaultAdmission)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100);
    h.sa.tick(1100 + 60'000); // Fault (DegradedTimeout)

    h.sa.arbitrate(activity_velocity());
    EXPECT_EQ(h.diag.activity_intents_rejected, 1u);

    Intent stop;
    stop.kind = IntentKind::Stop;
    stop.source = IntentSource::Safety;
    stop.stop_profile = StopProfile::Controlled;
    stop.seq = 99;
    h.sa.arbitrate(stop); // safety stop проходит (не отклоняется, #45 §4)
    EXPECT_TRUE(h.sa.intent_active());
    EXPECT_EQ(h.sa.current_intent().kind, IntentKind::Stop);
}

// T8: Маркер-вход (Q5 A): pending crash-маркер на init -> Fault (CrashMarker) +
// motion inhibited; power-cycle != acknowledgment (re-init с маркером -> Fault снова).
TEST(SafetyAuthority, CrashMarkerPendingFaults)
{
    Harness h;
    h.marker.set_pending(3);
    h.init();
    h.sa.tick(0);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(h.sa.fault(), SafetyFault::CrashMarker);
    EXPECT_FALSE(h.sa.motion_allowed());
    EXPECT_EQ(h.events.marker_count(), 1u);
    EXPECT_EQ(h.events.marker_crash(), 3u);

    // power-cycle: маркер не снят чтением -> снова Fault.
    h.sa.tick(100000);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
}

// T9: Crash-маркер запись: в Фазе 1 нет crash-триггеров - SA не вызывает write_crash.
TEST(SafetyAuthority, NoCrashWriteInPhase1)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100);
    h.sa.tick(1100 + 60'000); // Fault (DegradedTimeout - не crash-класс)
    EXPECT_EQ(h.marker.write_count(), 0u);
}

// T10: Слот bounded: 10k тиков детерминированно завершаются (структурно O(1);
// бюджет слота <= T_step измеряется kernel'ом #70 + L4).
TEST(SafetyAuthority, TickIsBoundedAndDeterministic)
{
    Harness h;
    h.init();
    for (std::uint32_t i = 0; i < 10'000; ++i)
    {
        h.sa.tick(i * 10u);
    }
    // Не зависло, FSM консистентен.
    EXPECT_TRUE(h.sa.health() == SafetyHealth::Ready ||
                h.sa.health() == SafetyHealth::Degraded);
}

// T22: INV-CAN-FAILSAFE: ErrorPassive -> stop-intent FORCE-STOP + fault CanFailsafe.
TEST(SafetyAuthority, CanErrorPassiveForcesStopAndFault)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready
    h.can.set_error_state(v3::CanErrorState::ErrorPassive);
    h.sa.tick(1100);

    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(h.sa.fault(), SafetyFault::CanFailsafe);
    EXPECT_EQ(h.events.can_failsafe_count(), 1u);
    EXPECT_EQ(h.events.stop_count(), 1u);
    EXPECT_EQ(h.events.last_profile(), StopProfile::ForceStop);
    EXPECT_EQ(h.sa.current_intent().kind, IntentKind::ForceStop);
}

// T20: Force-stop из слота (T_fs): force-stop intent текущий -> force_stop_tx() на
// КАЖДОЙ границе шага (не через actuation_step/FIFO).
TEST(SafetyAuthority, ForceStopEmittedFromEveryStepBoundary)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.can.set_error_state(v3::CanErrorState::ErrorPassive);
    h.sa.tick(1100); // Fault + force-stop intent
    const std::uint32_t after_latch = h.can.force_stop_count();
    EXPECT_GE(after_latch, 1u);

    // Каждый последующий тик повторяет force-stop (всегда транслируем).
    h.sa.tick(1200);
    h.sa.tick(1300);
    EXPECT_EQ(h.can.force_stop_count(), after_latch + 2u);
}

// T23: BusOff -> recover_bus_off вызван + fault; pending force-stop ретранслируется.
TEST(SafetyAuthority, BusOffRecoveryAndFault)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.can.set_error_state(v3::CanErrorState::BusOff);
    h.sa.tick(1100);

    EXPECT_EQ(h.can.recover_count(), 1u);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(h.sa.fault(), SafetyFault::CanFailsafe);
    EXPECT_GE(h.can.force_stop_count(), 1u); // pending force-stop ретранслируется
}

// T26: NTP-скачок (backward) не ломает T_deg/grace (monotonic clamp).
TEST(SafetyAuthority, BackwardJumpDoesNotBreakTdeg)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100); // Degraded, таймер пошёл

    h.sa.tick(1500); // нормальный тик
    const std::uint64_t acc = h.diag.degraded_motion_ms;
    EXPECT_GT(acc, 0u);

    h.sa.tick(1000); // backward jump (clamp): аккумулятор не растёт, ложного Fault нет
    EXPECT_EQ(h.sa.health(), SafetyHealth::Degraded);
    EXPECT_EQ(h.diag.degraded_motion_ms, acc);
}

// T27: Auto-clear recovery: Fault(DegradedTimeout) -> условие снято + stationary
// -> Degraded (ре-квалификация) -> Ready.
TEST(SafetyAuthority, AutoClearRecoveryFromFault)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100);
    h.sa.tick(1100 + 60'000); // Fault (DegradedTimeout)

    h.sensing.set_fresh_all(0); // условие снято
    h.sa.tick(1100 + 61'000); // -> Degraded (ре-квалификация)
    EXPECT_EQ(h.sa.health(), SafetyHealth::Degraded);
    EXPECT_EQ(h.diag.degraded_motion_ms, 0u); // таймер рестартовал

    h.sa.tick(1100 + 62'000); // Degraded с None -> Ready
    EXPECT_EQ(h.sa.health(), SafetyHealth::Ready);
}

// MAJOR-фикс ревью: после auto-clear recovery воронка освобождает stop-intent -
// движение восстанавливается (activity velocity принимается после Ready), иначе
// система навсегда в Ready с текущим Stop/ForceStop (функциональный тупик).
TEST(SafetyAuthority, RecoveryRestoresMotion)
{
    Harness h;
    h.init();
    h.sa.tick(1000); // Ready
    h.sensing.set_faulted(SensorId::TofChannelForward);
    h.sa.tick(1100); // Degraded
    h.sa.tick(1100 + 60'000); // Fault (DegradedTimeout) + stop CONTROLLED
    EXPECT_TRUE(h.sa.intent_active()); // stop-intent активен в Fault

    h.sensing.set_fresh_all(0); // условие снято
    h.sa.tick(1100 + 61'000); // Degraded (ре-квалификация, воронка освобождена)
    EXPECT_EQ(h.sa.health(), SafetyHealth::Degraded);
    EXPECT_FALSE(h.sa.intent_active()); // stop освобождён (review MAJOR)
    EXPECT_EQ(h.sa.fault(), SafetyFault::None); // latched fault сброшен (§2.5)

    h.sa.tick(1100 + 62'000); // Ready
    EXPECT_EQ(h.sa.health(), SafetyHealth::Ready);

    // Движение восстанавливается: activity velocity принимается (fail-on-bug).
    h.sa.arbitrate(activity_velocity());
    EXPECT_EQ(h.diag.activity_intents_rejected, 0u);
    EXPECT_TRUE(h.sa.intent_active());
    EXPECT_EQ(h.sa.current_intent().kind, IntentKind::VelocitySetpoint);
}

// T28: CrashMarker не снимается ни условием, ни power-cycle (Q5 A); clear не вызывается.
TEST(SafetyAuthority, CrashMarkerNotCleared)
{
    Harness h;
    h.marker.set_pending(7);
    h.init();
    h.sa.tick(0); // Fault (CrashMarker)

    h.sensing.set_fresh_all(0); // условие здорово - но crash-класс требует явный ack
    h.sa.tick(1000);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(h.sa.fault(), SafetyFault::CrashMarker);

    // power-cycle: маркер не снят -> снова Fault.
    Harness h2;
    h2.marker.set_pending(7);
    h2.init();
    h2.sa.tick(0);
    EXPECT_EQ(h2.sa.health(), SafetyHealth::Fault);

    EXPECT_EQ(h.marker.clear_count(), 0u);
    EXPECT_EQ(h.marker.read_count(), 1u);
}

// T31: CanFailsafe не auto-clear (HZ-03): восстановление шины не снимает fault;
// CrashMarker не затирается ErrorPassive (guard).
TEST(SafetyAuthority, CanFailsafeNotAutoCleared)
{
    Harness h;
    h.init();
    h.sa.tick(1000);
    h.can.set_error_state(v3::CanErrorState::ErrorPassive);
    h.sa.tick(1100); // Fault (CanFailsafe)

    // Шина восстановилась, сенсорика здорова - fault остаётся (нужен явный reset, Фаза 2+).
    h.can.set_error_state(v3::CanErrorState::Active);
    h.sensing.set_fresh_all(0);
    h.sa.tick(1200);
    h.sa.tick(1300);
    EXPECT_EQ(h.sa.health(), SafetyHealth::Fault);
    EXPECT_EQ(h.sa.fault(), SafetyFault::CanFailsafe);
}

TEST(SafetyAuthority, CrashMarkerNotOverwrittenByErrorPassive)
{
    Harness h;
    h.marker.set_pending(1);
    h.init();
    h.sa.tick(0); // Fault (CrashMarker)

    h.can.set_error_state(v3::CanErrorState::ErrorPassive);
    h.sa.tick(1000);

    // Guard: CrashMarker не затирается CanFailsafe (Q5 A); force-stop эмитится.
    EXPECT_EQ(h.sa.fault(), SafetyFault::CrashMarker);
    EXPECT_GE(h.can.force_stop_count(), 1u);
}

} // namespace
