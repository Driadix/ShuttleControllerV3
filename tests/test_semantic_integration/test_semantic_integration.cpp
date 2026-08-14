// End-to-end integration tests (design docs/operation-runtime-design-v3.md
// section 7.3 T40-T44): raw frame bytes -> codec -> inbound queue -> glue
// step -> Semantic Contract -> Operation Runtime -> outbound ACK; malformed
// rejection at every layer; bounded storage under flood (ledger eviction +
// instance cap); query/subscribe/stop control plane; single codec for both
// profiles (no transport-specific dialects).
#include <cstdint>

#include <gtest/gtest.h>

#include "domain/queues.h"
#include "domain/semantic.h"
#include "platform/admission_glue.h"
#include "tests/common/kernel_env.h"
#include "tests/common/semantic_fakes.h"

namespace
{

using v3::codec::AdmissionAckNegative;
using v3::codec::AdmissionAckPositive;
using v3::codec::CodecResult;
using v3::codec::DecodeResult;
using v3::codec::OperationRequest;
using v3::codec::RejectCode;
using v3::queue::Class;
using v3::queue::Frame;

struct IntEnv
{
    testfakes::KernelEnv kernel; // kernel + monotonic (glue re-arms through it)
    test::FakeEpoch epoch;
    test::FakeWindow window;
    test::FakeHealth health;
    test::FakeProvisioning prov;
    test::RecordingEvents events;
    test::RecordingOutbound out;
    v3::slot::ExclusiveSlot slot;
    v3::runtime::Runtime rt;
    v3::subscription::Registry subs;
    v3::semantic::TypeRegistry types;
    v3::semantic::SemanticContract sc;
    v3::queue::InboundQueue q;
    v3::glue::SemanticContext ctx;

    IntEnv()
    {
        kernel.init();
        epoch.set(7);
        subs.init(0, &events);
        rt.init(&epoch, &events, &slot);
        v3::semantic::Grant g;
        g.authority_id = 5;
        g.roles = v3::codec::RoleControlClient;
        sc.init(&epoch, &window, &health, &prov, &events, &out, &rt, &subs, &types, g);
        v3::semantic::OperationType t;
        t.id = 1;
        t.root_allowed = true;
        t.exclusive = true;
        t.activity = v3::slot::Activity::Motion;
        t.driver = test::driver_yield;
        types.register_type(t); // constructor: no ASSERT (non-void-safe); tests fail on use
        v3::semantic::OperationType t2; // non-exclusive: flood/fill without the slot
        t2.id = 2;
        t2.root_allowed = true;
        t2.exclusive = false;
        t2.driver = test::driver_yield;
        types.register_type(t2);
        ctx.inbound = &q;
        ctx.semantic = &sc;
        ctx.runtime = &rt;
        ctx.events = &events;
    }

    void drain()
    {
        v3::glue::inbound_tick(&ctx); // pops at most one Control frame per call
    }

    void push(const std::uint8_t* bytes, std::uint16_t len)
    {
        Frame f;
        f.len = len;
        for (std::uint16_t i = 0; i < len; ++i)
        {
            f.data[i] = bytes[i];
        }
        ASSERT_TRUE(q.push(Class::Control, f, false));
    }

    void send_request(const OperationRequest& r)
    {
        std::uint8_t frame[v3::codec::Mtu];
        const std::uint16_t flen = test::request_frame(r, frame, sizeof(frame));
        ASSERT_GT(flen, 0u);
        push(frame, flen);
        drain();
    }

    std::uint32_t last_ack_pos()
    {
        if (out.count() == 0)
        {
            ADD_FAILURE() << "no outbound frame captured";
            return 0;
        }
        const DecodeResult dr = v3::codec::decode(out.frame(out.count() - 1).data,
                                                  out.frame(out.count() - 1).len);
        EXPECT_TRUE(dr.ok());
        AdmissionAckPositive a;
        EXPECT_EQ(v3::codec::decode_ack_pos(dr.frame.payload, dr.frame.payload_len, a), CodecResult::Ok);
        return a.operation_id;
    }

    std::uint8_t last_reject()
    {
        if (out.count() == 0)
        {
            ADD_FAILURE() << "no outbound frame captured";
            return 0xFF;
        }
        const DecodeResult dr = v3::codec::decode(out.frame(out.count() - 1).data,
                                                  out.frame(out.count() - 1).len);
        EXPECT_TRUE(dr.ok());
        AdmissionAckNegative a;
        EXPECT_EQ(v3::codec::decode_ack_neg(dr.frame.payload, dr.frame.payload_len, a), CodecResult::Ok);
        return a.reject_code;
    }
};

OperationRequest req(std::uint32_t id, std::uint32_t epoch = 7)
{
    OperationRequest r;
    r.request_id = id;
    r.controller_epoch = epoch;
    r.authority_id = 5;
    r.role = v3::codec::RoleControlClient;
    r.operation_type = 1;
    r.parent_operation_id = 0;
    return r;
}

TEST(Integration, RawBytesToAckAndDuplicateReplay)
{
    IntEnv env;
    env.send_request(req(1));
    const std::uint32_t op = env.last_ack_pos();
    EXPECT_EQ(env.events.started_count, 1u);

    env.send_request(req(1)); // duplicate: same result, no new instance
    EXPECT_EQ(env.last_ack_pos(), op);
    EXPECT_EQ(env.events.started_count, 1u);
}

TEST(Integration, MalformedRejectedAtEveryLayer)
{
    IntEnv env;

    // Bad CRC: dropped at the codec (glue), transport_error event, no ACK.
    std::uint8_t frame[v3::codec::Mtu];
    const std::uint16_t flen = test::request_frame(req(2), frame, sizeof(frame));
    ASSERT_GT(flen, 0u);
    frame[12] ^= 0x01; // corrupt one payload bit
    env.push(frame, flen);
    env.drain();
    EXPECT_EQ(env.out.count(), 0u);
    ASSERT_EQ(env.events.error_count, 1u);

    // Truncated frame: dropped, no ACK.
    std::uint8_t short_frame[11] = {};
    env.push(short_frame, 11);
    env.drain();
    EXPECT_EQ(env.out.count(), 0u);
    EXPECT_EQ(env.events.error_count, 2u);

    // Wrong epoch: negative ACK (EpochMismatch), no instance.
    env.send_request(req(3, 99));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::EpochMismatch));
    EXPECT_EQ(env.events.started_count, 0u);
}

TEST(Integration, BoundedStorageUnderFlood)
{
    IntEnv env;
    // 20 distinct requests (non-exclusive type: no slot serialization);
    // driver_yield keeps instances active (no advance).
    for (std::uint32_t i = 0; i < 20; ++i)
    {
        OperationRequest r = req(1000 + i);
        r.operation_type = 2; // non-exclusive
        env.send_request(r);
    }
    // 8 accepted (runtime cap), 12 rejected InstancesFull; bounded storage.
    EXPECT_EQ(env.events.started_count, 8u);
    EXPECT_EQ(env.rt.active_count(), 8u);
    std::uint32_t full_rejects = 0;
    for (std::uint32_t i = 8; i < env.out.count(); ++i)
    {
        const DecodeResult dr = v3::codec::decode(env.out.frame(i).data, env.out.frame(i).len);
        ASSERT_TRUE(dr.ok());
        AdmissionAckNegative a;
        ASSERT_EQ(v3::codec::decode_ack_neg(dr.frame.payload, dr.frame.payload_len, a), CodecResult::Ok);
        if (a.reject_code == static_cast<std::uint8_t>(RejectCode::InstancesFull))
        {
            ++full_rejects;
        }
    }
    EXPECT_EQ(full_rejects, 12u);

    // Request #1 was evicted from the ledger (20 stores, depth 8 -> 12 evictions):
    // the protocol no longer promises to recognize the repeat (#13); re-admission
    // hits the instance cap again -> InstancesFull, NOT a duplicate replay.
    env.send_request(req(1000));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::InstancesFull));
    EXPECT_EQ(env.events.started_count, 8u); // still no new instance
}

TEST(Integration, QuerySubscribeStopControlPlane)
{
    IntEnv env;

    // Query in Boot: rejected (WrongWindow), nothing forwarded.
    env.window.set(v3::PlatformWindow::Boot);
    std::uint8_t query_frame[v3::codec::Mtu];
    const std::uint8_t query_payload[1] = {0xFF}; // sections_mask
    const std::uint16_t qflen = test::make_control_frame(
        static_cast<std::uint8_t>(v3::codec::MsgControl::Query),
        query_payload, sizeof(query_payload), query_frame, sizeof(query_frame));
    env.push(query_frame, qflen);
    env.drain();
    std::uint32_t before = env.out.count();
    EXPECT_EQ(before, 0u);

    // Query in Serving: forwarded to the outbound control path (snapshot
    // provider #72 answers, #49 section 2.6).
    env.window.set(v3::PlatformWindow::Serving);
    env.push(query_frame, qflen);
    env.drain();
    ASSERT_GT(env.out.count(), before);
    const DecodeResult qdr = v3::codec::decode(env.out.frame(env.out.count() - 1).data,
                                               env.out.frame(env.out.count() - 1).len);
    ASSERT_TRUE(qdr.ok());
    EXPECT_EQ(qdr.frame.header.msg_type, static_cast<std::uint8_t>(v3::codec::MsgControl::Query));

    // Subscribe: SubscriptionAck accepted + birth pending.
    v3::codec::Subscribe s;
    s.class_mask = v3::codec::ClassBitTelemetry;
    s.min_interval_ms = 300;
    s.max_bytes_per_tick = 128;
    std::uint8_t sub_payload[6] = {s.class_mask, s.filter, 0x2C, 0x01, 0x80, 0x00};
    std::uint8_t sub_frame[v3::codec::Mtu];
    const std::uint16_t sflen = test::make_control_frame(
        static_cast<std::uint8_t>(v3::codec::MsgControl::Subscribe),
        sub_payload, sizeof(sub_payload), sub_frame, sizeof(sub_frame));
    env.push(sub_frame, sflen);
    env.drain();
    ASSERT_GT(env.out.count(), 0u);
    const DecodeResult sdr = v3::codec::decode(env.out.frame(env.out.count() - 1).data,
                                               env.out.frame(env.out.count() - 1).len);
    ASSERT_TRUE(sdr.ok());
    v3::codec::SubscriptionAck ack;
    ASSERT_EQ(v3::codec::decode_sub_ack(sdr.frame.payload, sdr.frame.payload_len, ack), CodecResult::Ok);
    EXPECT_TRUE(ack.accepted);
    EXPECT_TRUE(env.subs.birth_pending(5));
    EXPECT_TRUE(env.subs.interest(v3::codec::QueueClass::Telemetry));

    // Stop intent: silent, idempotent; instance -> Stopping.
    env.send_request(req(77));
    EXPECT_EQ(env.last_ack_pos() != 0u, true);
    std::uint8_t stop_payload[4] = {0, 0, 0, 0}; // placeholder; filled below
    const std::uint32_t op_id = env.rt.active_count() > 0 ? 0 : 0;
    (void)op_id;
    // Find the created instance id from the ACK.
    const DecodeResult adr = v3::codec::decode(env.out.frame(env.out.count() - 1).data,
                                               env.out.frame(env.out.count() - 1).len);
    ASSERT_TRUE(adr.ok());
    v3::codec::AdmissionAckPositive apos;
    ASSERT_EQ(v3::codec::decode_ack_pos(adr.frame.payload, adr.frame.payload_len, apos), CodecResult::Ok);
    stop_payload[0] = static_cast<std::uint8_t>(apos.operation_id & 0xFFu);
    stop_payload[1] = static_cast<std::uint8_t>((apos.operation_id >> 8) & 0xFFu);
    stop_payload[2] = static_cast<std::uint8_t>((apos.operation_id >> 16) & 0xFFu);
    stop_payload[3] = static_cast<std::uint8_t>((apos.operation_id >> 24) & 0xFFu);
    std::uint8_t stop_frame[v3::codec::Mtu];
    const std::uint16_t stop_len = test::make_control_frame(
        static_cast<std::uint8_t>(v3::codec::MsgControl::StopIntent),
        stop_payload, sizeof(stop_payload), stop_frame, sizeof(stop_frame));
    env.push(stop_frame, stop_len);
    env.drain();
    std::uint32_t snap_count = 0;
    const v3::runtime::Instance* snap = env.rt.snapshot(snap_count);
    bool found_stopping = false;
    for (std::uint32_t i = 0; i < snap_count; ++i)
    {
        if (snap[i].op_id == apos.operation_id)
        {
            found_stopping = snap[i].state == v3::runtime::OpState::Stopping;
        }
    }
    EXPECT_TRUE(found_stopping); // stop intent applied, silently (no ACK)
}

TEST(Integration, SingleCodecServesBothProfiles)
{
    // No transport-specific semantic dialect (acceptance #74): the codec is
    // stateless w.r.t. the effective profile - switching the subscription
    // profile does not change frame decoding.
    IntEnv env;
    std::uint8_t frame[v3::codec::Mtu];
    const std::uint16_t flen = test::request_frame(req(9), frame, sizeof(frame));
    ASSERT_GT(flen, 0u);
    const DecodeResult before = v3::codec::decode(frame, flen);
    ASSERT_TRUE(before.ok());

    env.subs.init(/*radio*/ 1, &env.events); // effective profile change (#75 supplies)
    const DecodeResult after = v3::codec::decode(frame, flen);
    ASSERT_TRUE(after.ok());
    EXPECT_EQ(after.frame.header.msg_family, before.frame.header.msg_family);
    EXPECT_EQ(after.frame.header.msg_type, before.frame.header.msg_type);
    EXPECT_EQ(after.frame.payload_len, before.frame.payload_len);
}

} // namespace
