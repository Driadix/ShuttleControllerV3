// Queue classes and overload policies (issue #43 section 6, budgets #48
// section 6): Control 18 (16+2 reserve), Service 8, Update 4; egress
// telemetry/traces drop-oldest, events/logs drop-newest; every drop/reject
// increments an observable counter (obligation #7).
#include <gtest/gtest.h>

#include "domain/queues.h"

namespace
{

using slice::Byte;
using slice::QueueClasses;

TEST(QueueClasses, ControlQueueKeepsReserveForStop)
{
    QueueClasses q;
    // 16 working slots fill; 2 reserve slots stay free for stop/handshake.
    for (std::uint32_t i = 0; i < 16; ++i)
    {
        EXPECT_TRUE(q.control_push(static_cast<slice::ControlFrame>(i)));
    }
    EXPECT_EQ(q.control_size(), 16);
    EXPECT_EQ(q.rejected_control(), 0);
    // The 2 reserve slots still admit (stop/handshake never rejected).
    EXPECT_TRUE(q.control_push(100));
    EXPECT_TRUE(q.control_push(101));
    // 19th frame is rejected on admission (observable counter).
    EXPECT_FALSE(q.control_push(102));
    EXPECT_EQ(q.rejected_control(), 1);
}

TEST(QueueClasses, ServiceQueueRejectsOnFull)
{
    QueueClasses q;
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        EXPECT_TRUE(q.service_push(static_cast<slice::ControlFrame>(i)));
    }
    EXPECT_FALSE(q.service_push(9));
}

TEST(QueueClasses, EventsLogsDropNewestOnOverflow)
{
    QueueClasses q;
    for (std::uint32_t i = 0; i < 32; ++i)
    {
        q.events_push(static_cast<Byte>(i));
    }
    EXPECT_EQ(q.dropped_events(), 0);
    q.events_push(0xFF); // 33rd: drop-newest (newest rejected, queue intact)
    EXPECT_EQ(q.dropped_events(), 1);
    EXPECT_EQ(q.dropped_logs(), 0);
}

TEST(QueueClasses, TelemetryDropsOldestOnOverflow)
{
    QueueClasses q;
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        q.telemetry_push(static_cast<Byte>(i));
    }
    q.telemetry_push(0xAA); // full: drop-oldest (freshness over completeness)
    EXPECT_EQ(q.dropped_telemetry(), 1);
    EXPECT_EQ(q.telemetry_size(), 8); // still full with the freshest 8
}

TEST(QueueClasses, OverflowCountersAreObservable)
{
    QueueClasses q;
    for (std::uint32_t i = 0; i < 100; ++i)
    {
        q.logs_push(static_cast<Byte>(i));
    }
    EXPECT_EQ(q.dropped_logs(), 100 - 32);
}

} // namespace
