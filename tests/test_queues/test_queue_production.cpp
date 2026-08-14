// Production inbound queue tests (design docs/operation-runtime-design-v3.md
// section 7.3 T10-T13; #43 section 6, #48 section 6): working capacity +
// reject on admission, reserve slots (stop/handshake never rejected), class
// capacities, FIFO order, observable counters.
#include <cstdint>

#include <gtest/gtest.h>

#include "domain/queues.h"

namespace
{

using v3::queue::Class;
using v3::queue::Frame;
using v3::queue::InboundQueue;

Frame make_frame(std::uint8_t tag)
{
    Frame f;
    f.data[0] = tag;
    f.len = 1;
    return f;
}

TEST(QueueProduction, ControlWorkingCapacityRejectsOnAdmission)
{
    InboundQueue q;
    for (std::uint32_t i = 0; i < InboundQueue::ControlCapacity; ++i)
    {
        EXPECT_TRUE(q.push(Class::Control, make_frame(static_cast<std::uint8_t>(i)), false));
    }
    EXPECT_EQ(q.size(Class::Control), InboundQueue::ControlCapacity);
    EXPECT_EQ(q.rejected(Class::Control), 0u);
    // 17th ordinary frame: rejected on admission (observable counter, obs #7).
    EXPECT_FALSE(q.push(Class::Control, make_frame(0xFF), false));
    EXPECT_EQ(q.rejected(Class::Control), 1u);
}

TEST(QueueProduction, ReserveSlotsNeverRejectStopAndDrainFirst)
{
    InboundQueue q;
    for (std::uint32_t i = 0; i < InboundQueue::ControlCapacity; ++i)
    {
        ASSERT_TRUE(q.push(Class::Control, make_frame(static_cast<std::uint8_t>(i)), false));
    }
    // stop/handshake (reserve) still admitted into the 2 reserve slots.
    EXPECT_TRUE(q.push(Class::Control, make_frame(0xE1), true));
    EXPECT_TRUE(q.push(Class::Control, make_frame(0xE2), true));
    // Physically full (16+2): a further reserve cannot be admitted; NOT counted
    // as a reject-on-admission (it is reserve exhaustion, not a class reject).
    EXPECT_FALSE(q.push(Class::Control, make_frame(0xE3), true));
    EXPECT_EQ(q.rejected(Class::Control), 0u);
    // Drain priority: the 2 reserve (stop) frames come out first.
    Frame out;
    ASSERT_TRUE(q.pop(Class::Control, out));
    EXPECT_EQ(out.data[0], 0xE1);
    ASSERT_TRUE(q.pop(Class::Control, out));
    EXPECT_EQ(out.data[0], 0xE2);
    ASSERT_TRUE(q.pop(Class::Control, out));
    EXPECT_EQ(out.data[0], 0);
}

TEST(QueueProduction, ServiceAndUpdateCapacities)
{
    InboundQueue q;
    for (std::uint32_t i = 0; i < InboundQueue::ServiceCapacity; ++i)
    {
        EXPECT_TRUE(q.push(Class::Service, make_frame(1), false));
    }
    EXPECT_FALSE(q.push(Class::Service, make_frame(1), false));
    EXPECT_EQ(q.rejected(Class::Service), 1u);

    // Update: 2 working (new transactions) + 2 reserve (in-progress, #48 §6).
    for (std::uint32_t i = 0; i < InboundQueue::UpdateCapacity; ++i)
    {
        EXPECT_TRUE(q.push(Class::Update, make_frame(2), false));
    }
    // New transaction when working is full: rejected (new transactions only).
    EXPECT_FALSE(q.push(Class::Update, make_frame(2), false));
    EXPECT_EQ(q.rejected(Class::Update), 1u);
    // In-progress frames (reserve) are never rejected by the full working set.
    EXPECT_TRUE(q.push(Class::Update, make_frame(2), true));
    EXPECT_TRUE(q.push(Class::Update, make_frame(2), true));
    EXPECT_FALSE(q.push(Class::Update, make_frame(2), true)); // physically full (2+2)
    // Reserve drains first.
    Frame out;
    ASSERT_TRUE(q.pop(Class::Update, out));
    ASSERT_TRUE(q.pop(Class::Update, out));
    ASSERT_TRUE(q.pop(Class::Update, out));
    EXPECT_EQ(q.rejected(Class::Update), 1u); // reserve exhaustion is not a reject
}

TEST(QueueProduction, FifoOrderAndObservableCounters)
{
    InboundQueue q;
    ASSERT_TRUE(q.push(Class::Control, make_frame(10), false));
    ASSERT_TRUE(q.push(Class::Control, make_frame(11), false));
    ASSERT_TRUE(q.push(Class::Control, make_frame(12), false));
    Frame out;
    ASSERT_TRUE(q.pop(Class::Control, out));
    EXPECT_EQ(out.data[0], 10);
    ASSERT_TRUE(q.pop(Class::Control, out));
    EXPECT_EQ(out.data[0], 11);
    ASSERT_TRUE(q.pop(Class::Control, out));
    EXPECT_EQ(out.data[0], 12);
    EXPECT_FALSE(q.pop(Class::Control, out));
    EXPECT_TRUE(q.is_full(Class::Control) == false);
    EXPECT_EQ(q.rejected(Class::Control), 0u);
}

} // namespace
