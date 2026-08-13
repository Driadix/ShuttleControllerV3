// test_events suite: the recording KernelEvents sink captures all four kernel
// event types (design docs/execution-foundation-design-v3.md section 7.3 T13).
// Host, deterministic.
#include <gtest/gtest.h>

#include "tests/common/fakes.h"

TEST(EventsTest, RecordingSinkCapturesAllEvents)
{
    testfakes::RecordingEvents events;

    events.step_overrun(12);
    events.step_overrun(25);
    events.scheduler_gap(31);
    events.schedule_rejected();
    events.reset_cause(v3::ResetCause::Watchdog);

    ASSERT_EQ(events.overrun_count(), 2u);
    EXPECT_EQ(events.overrun(0), 12u);
    EXPECT_EQ(events.overrun(1), 25u);

    ASSERT_EQ(events.gap_count(), 1u);
    EXPECT_EQ(events.gap(0), 31u);

    EXPECT_EQ(events.rejected(), 1u);

    ASSERT_EQ(events.cause_count(), 1u);
    EXPECT_EQ(events.cause(0), v3::ResetCause::Watchdog);
}

TEST(EventsTest, ResetCauseEnumValues)
{
    // Contract stability: enum values are part of the wire/record format
    // (crash record through reboot, #49 section 13).
    EXPECT_EQ(static_cast<int>(v3::ResetCause::PowerOn), 0);
    EXPECT_EQ(static_cast<int>(v3::ResetCause::Watchdog), 1);
    EXPECT_EQ(static_cast<int>(v3::ResetCause::Software), 2);
    EXPECT_EQ(static_cast<int>(v3::ResetCause::External), 3);
    EXPECT_EQ(static_cast<int>(v3::ResetCause::Unknown), 4);
}
