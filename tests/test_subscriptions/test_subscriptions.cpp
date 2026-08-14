// Subscription registry tests (design docs/operation-runtime-design-v3.md
// section 7.3 T35-T39; #49 section 9): caps (bridge 8 / radio 2), interest
// computation + profile defaults, birth pattern, epoch reset, per-principal
// isolation, slow-consumer drop observability.
#include <cstdint>

#include <gtest/gtest.h>

#include "domain/subscriptions.h"
#include "tests/common/semantic_fakes.h"

namespace
{

using v3::codec::QueueClass;
using v3::codec::Subscribe;
using v3::subscription::Registry;

v3::codec::Subscribe make_sub(std::uint8_t mask)
{
    v3::codec::Subscribe s;
    s.class_mask = mask;
    s.min_interval_ms = 300;
    s.max_bytes_per_tick = 128;
    return s;
}

TEST(Subscriptions, BridgeCapEightRadioCapTwo)
{
    test::RecordingEvents events;
    Registry r;
    r.init(/*bridge*/ 0, &events);
    std::uint8_t masks[8] = {0x01, 0x02, 0x04, 0x08, 0x03, 0x05, 0x06, 0x07};
    for (std::uint8_t i = 0; i < 8; ++i)
    {
        std::uint8_t id = 0;
        EXPECT_EQ(r.subscribe(1, make_sub(masks[i]), id), Registry::Result::Ok);
    }
    EXPECT_EQ(r.active_count(), 8u);
    std::uint8_t id = 0;
    EXPECT_EQ(r.subscribe(1, make_sub(0x0F), id), Registry::Result::CapsExceeded); // 9th

    Registry rr;
    rr.init(/*radio*/ 1, &events);
    std::uint8_t id2 = 0;
    EXPECT_EQ(rr.subscribe(1, make_sub(0x01), id2), Registry::Result::Ok);
    EXPECT_EQ(rr.subscribe(1, make_sub(0x02), id2), Registry::Result::Ok);
    EXPECT_EQ(rr.subscribe(1, make_sub(0x04), id2), Registry::Result::CapsExceeded); // radio cap 2
}

TEST(Subscriptions, InterestFollowsProfileDefaults)
{
    test::RecordingEvents events;
    Registry r;
    r.init(/*bridge*/ 0, &events);
    // Bridge defaults (#49 section 9): telemetry + events always, logs/traces
    // push only by subscription.
    EXPECT_TRUE(r.interest(QueueClass::Telemetry));
    EXPECT_TRUE(r.interest(QueueClass::Events));
    EXPECT_FALSE(r.interest(QueueClass::Logs));
    EXPECT_FALSE(r.interest(QueueClass::Traces));

    std::uint8_t id = 0;
    EXPECT_EQ(r.subscribe(1, make_sub(0x04), id), Registry::Result::Ok); // logs
    EXPECT_TRUE(r.interest(QueueClass::Logs));
    EXPECT_FALSE(r.interest(QueueClass::Traces));

    Registry rr;
    rr.init(/*radio*/ 1, &events);
    EXPECT_TRUE(rr.interest(QueueClass::Events));                 // events always (reserve)
    EXPECT_FALSE(rr.interest(QueueClass::Telemetry));             // radio: by subscription only
    std::uint8_t id2 = 0;
    EXPECT_EQ(rr.subscribe(1, make_sub(0x01), id2), Registry::Result::Ok); // telemetry
    EXPECT_TRUE(rr.interest(QueueClass::Telemetry));
}

TEST(Subscriptions, BirthOnSubscribeAndEpochReset)
{
    test::RecordingEvents events;
    Registry r;
    r.init(0, &events);
    std::uint8_t id = 0;
    EXPECT_EQ(r.subscribe(1, make_sub(0x01), id), Registry::Result::Ok);
    EXPECT_TRUE(r.birth_pending(1));
    r.birth_sent(1);
    EXPECT_FALSE(r.birth_pending(1));
    // (re)subscribe: birth again (#49 section 2.6).
    EXPECT_EQ(r.subscribe(1, make_sub(0x01), id), Registry::Result::Ok);
    EXPECT_TRUE(r.birth_pending(1));
    // Epoch reset: all subscriptions die (#49 section 9; #46 I-LC-6).
    r.epoch_reset();
    EXPECT_EQ(r.active_count(), 0u);
    EXPECT_FALSE(r.birth_pending(1));
}

TEST(Subscriptions, PerPrincipalIsolation)
{
    test::RecordingEvents events;
    Registry r;
    r.init(0, &events);
    std::uint8_t id = 0;
    ASSERT_EQ(r.subscribe(1, make_sub(0x01), id), Registry::Result::Ok);
    EXPECT_EQ(r.unsubscribe(2, id), Registry::Result::UnknownSub); // foreign principal
    EXPECT_EQ(r.active_count(), 1u);
    EXPECT_EQ(r.unsubscribe(1, id), Registry::Result::Ok);
    EXPECT_EQ(r.active_count(), 0u);
}

TEST(Subscriptions, SlowConsumerDropObservable)
{
    test::RecordingEvents events;
    Registry r;
    r.init(0, &events);
    std::uint8_t id = 0;
    ASSERT_EQ(r.subscribe(1, make_sub(0x01), id), Registry::Result::Ok);
    r.note_drop(id);
    r.note_drop(id);
    EXPECT_EQ(r.drops(), 2u);
    EXPECT_EQ(events.drop_count, 2u);
    EXPECT_EQ(events.drops[0], id);
}

} // namespace
