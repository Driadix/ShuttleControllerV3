// Semantic Contract & Admission tests (design
// docs/operation-runtime-design-v3.md section 7.3 T19-T26; #13 admission
// steps 1-7, #46 section 8 matrix): first-error-wins order, idempotency at
// normative step 5 (replay without re-reservation), replay of stored
// rejections, negative ACK shape, gate matrix, reservation rollback, roles.
#include <cstdint>

#include <gtest/gtest.h>

#include "domain/semantic.h"
#include "tests/common/semantic_fakes.h"

namespace
{

using v3::codec::AdmissionAckNegative;
using v3::codec::AdmissionAckPositive;
using v3::codec::CodecResult;
using v3::codec::DecodeResult;
using v3::codec::OperationRequest;
using v3::codec::RejectCode;
using v3::runtime::DriverFn;
using v3::semantic::Grant;
using v3::semantic::SemanticContract;
using v3::semantic::TypeRegistry;

class SemEnv
{
  public:
    test::FakeEpoch epoch;
    test::FakeWindow window;
    test::FakeHealth health;
    test::FakeProvisioning prov;
    test::RecordingEvents events;
    test::RecordingOutbound out;
    v3::slot::ExclusiveSlot slot;
    v3::runtime::Runtime rt;
    v3::subscription::Registry subs;
    TypeRegistry types;
    SemanticContract sc;

    SemEnv(std::uint16_t grant_authority = 5, std::uint8_t grant_roles = v3::codec::RoleControlClient)
    {
        epoch.set(7);
        subs.init(0, &events);
        rt.init(&epoch, &events, &slot);
        Grant g;
        g.authority_id = grant_authority;
        g.roles = grant_roles;
        sc.init(&epoch, &window, &health, &prov, &events, &out, &rt, &subs, &types, g);
    }

    void register_type(std::uint16_t id, bool child_allowed = false, bool exclusive = true,
                       v3::slot::Activity act = v3::slot::Activity::Motion,
                       std::uint8_t params_max = 64, DriverFn fn = test::driver_yield)
    {
        v3::semantic::OperationType t;
        t.id = id;
        t.root_allowed = true;
        t.child_allowed = child_allowed;
        t.exclusive = exclusive;
        t.activity = act;
        t.params_max = params_max;
        t.driver = fn;
        ASSERT_TRUE(types.register_type(t));
    }

    // Sends one OperationRequest frame through the pipeline.
    void send(const OperationRequest& r)
    {
        std::uint8_t frame[v3::codec::Mtu];
        const std::uint16_t flen = test::request_frame(r, frame, sizeof(frame));
        ASSERT_GT(flen, 0u);
        const DecodeResult dr = v3::codec::decode(frame, flen);
        ASSERT_TRUE(dr.ok());
        sc.process_frame(dr.frame);
    }

    // Sends an arbitrary Control-family frame (payload pre-encoded by the caller).
    void send_frame(std::uint8_t msg_type, const std::uint8_t* payload, std::uint16_t len)
    {
        std::uint8_t frame[v3::codec::Mtu];
        const std::uint16_t flen = test::make_control_frame(msg_type, payload, len, frame, sizeof(frame));
        ASSERT_GT(flen, 0u);
        const DecodeResult dr = v3::codec::decode(frame, flen);
        ASSERT_TRUE(dr.ok());
        sc.process_frame(dr.frame);
    }

    // Sends a StopIntent frame for op_id.
    void send_stop(std::uint32_t op_id)
    {
        std::uint8_t payload[4] = {static_cast<std::uint8_t>(op_id & 0xFFu),
                                   static_cast<std::uint8_t>((op_id >> 8) & 0xFFu),
                                   static_cast<std::uint8_t>((op_id >> 16) & 0xFFu),
                                   static_cast<std::uint8_t>((op_id >> 24) & 0xFFu)};
        send_frame(static_cast<std::uint8_t>(v3::codec::MsgControl::StopIntent), payload, 4);
    }

    // Sends a Subscribe frame.
    void send_subscribe(std::uint8_t class_mask)
    {
        std::uint8_t payload[6] = {class_mask, 0, 0x2C, 0x01, 0x80, 0x00}; // 300 ms, 128 B/tick
        send_frame(static_cast<std::uint8_t>(v3::codec::MsgControl::Subscribe), payload, 6);
    }

    // Decodes the last outbound frame as a positive ACK; returns its op id.
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
        EXPECT_EQ(v3::codec::decode_ack_pos(dr.frame.payload, dr.frame.payload_len, a),
                  CodecResult::Ok);
        return a.operation_id;
    }

    // Decodes the last outbound frame as a negative ACK; returns the reject code.
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
        EXPECT_EQ(v3::codec::decode_ack_neg(dr.frame.payload, dr.frame.payload_len, a),
                  CodecResult::Ok);
        return a.reject_code;
    }
};

OperationRequest req(std::uint32_t id, std::uint32_t epoch = 7, std::uint16_t authority = 5,
                     std::uint8_t role = v3::codec::RoleControlClient, std::uint16_t type = 1,
                     std::uint32_t parent = 0, std::uint8_t params_len = 0)
{
    OperationRequest r;
    r.request_id = id;
    r.controller_epoch = epoch;
    r.authority_id = authority;
    r.role = role;
    r.operation_type = type;
    r.parent_operation_id = parent;
    r.params_len = params_len;
    return r;
}

TEST(Semantic, FirstErrorWinsAcrossAdmissionSteps)
{
    SemEnv env;
    env.register_type(1);

    env.send(req(1, /*epoch*/ 99)); // step 1: epoch
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::EpochMismatch));

    env.send(req(2, 7, /*authority*/ 6)); // step 1: echo mismatch
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::Unauthorized));

    env.send(req(3, 7, 5, /*role*/ v3::codec::RoleServiceClient)); // step 1: role outside grant
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::RoleEscalation));

    env.send(req(4, 7, 5, v3::codec::RoleControlClient, /*type*/ 99)); // steps 2-4: unknown type
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::UnknownOperationType));

    env.register_type(2, false, true, v3::slot::Activity::Motion, /*params_max*/ 4);
    env.send(req(5, 7, 5, v3::codec::RoleControlClient, 2, 0, /*params_len*/ 8));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::InvalidParameters));

    env.send(req(6, 7, 5, v3::codec::RoleControlClient, 1, /*parent*/ 42)); // child, no child_allowed
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::CompositionInvalid));

    EXPECT_EQ(env.events.started_count, 0u); // nothing was created
}

TEST(Semantic, IdempotencyHitReplaysWithoutNewInstance)
{
    SemEnv env;
    env.register_type(1);

    env.send(req(1));
    const std::uint32_t first = env.last_ack_pos();
    EXPECT_EQ(env.events.started_count, 1u);

    // Same key + same payload: stored result replayed, NO new instance (#13).
    env.send(req(1));
    EXPECT_EQ(env.last_ack_pos(), first);
    EXPECT_EQ(env.events.started_count, 1u);
    EXPECT_EQ(env.events.dup_count, 1u);

    // Same key + different payload: Conflict, no instance.
    OperationRequest r = req(1);
    r.params[0] = 0xFF;
    r.params_len = 1;
    env.send(r);
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::Conflict));
    EXPECT_EQ(env.events.started_count, 1u);
}

TEST(Semantic, ReplayReturnsStoredResultDespiteChangedGates)
{
    SemEnv env;
    env.register_type(1); // driver_yield: stays Running, slot held

    env.send(req(1));
    const std::uint32_t op = env.last_ack_pos();
    EXPECT_TRUE(env.rt.slot_held());
    EXPECT_EQ(env.events.started_count, 1u);

    // Gates changed (health -> Fault): replay must return the SAME result
    // without re-reserving (owner decision section 0.4; #13).
    env.health.set(v3::safety::SafetyHealth::Fault);
    env.send(req(1));
    EXPECT_EQ(env.last_ack_pos(), op);
    EXPECT_EQ(env.events.started_count, 1u); // no new instance
    EXPECT_EQ(env.events.reject_count, 0u);  // no re-admission attempt
}

TEST(Semantic, StoredRejectionReplayedAfterGatesRecover)
{
    SemEnv env;
    env.register_type(1);
    env.health.set(v3::safety::SafetyHealth::Fault);
    env.send(req(1));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::HealthGate));

    // Gates recovered: the replay still returns the STORED rejection (#13
    // «тот же admission result»; design section 2.4: rejected outcomes stored).
    env.health.set(v3::safety::SafetyHealth::Ready);
    env.send(req(1));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::HealthGate));
    EXPECT_EQ(env.events.started_count, 0u);
}

TEST(Semantic, GateMatrixWindowHealthProvisioningSlot)
{
    SemEnv env;
    env.register_type(1);

    env.window.set(v3::PlatformWindow::Update);
    env.send(req(1));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::WrongWindow));

    env.window.set(v3::PlatformWindow::Serving);
    env.health.set(v3::safety::SafetyHealth::Fault);
    env.send(req(2));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::HealthGate));

    env.health.set(v3::safety::SafetyHealth::Ready);
    env.prov.set(v3::ProvisioningStatus::Unprovisioned);
    env.send(req(3));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::ProvisioningGate));

    env.prov.set(v3::ProvisioningStatus::Provisioned);
    env.send(req(4)); // accepted: exclusive root, slot claimed, stays Running
    ASSERT_EQ(env.events.started_count, 1u);
    EXPECT_TRUE(env.rt.slot_held());

    env.send(req(5)); // slot busy: ResourceConflict (I-LC-4)
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::ResourceConflict));
}

TEST(Semantic, NoPartialReservationOnFullRuntime)
{
    SemEnv env;
    env.register_type(1, false, /*exclusive*/ false); // non-exclusive: no slot
    env.register_type(2, false, /*exclusive*/ true, v3::slot::Activity::Motion);

    for (std::uint32_t i = 0; i < v3::runtime::Runtime::MaxActiveInstances; ++i)
    {
        env.send(req(static_cast<std::uint32_t>(100 + i), 7, 5, v3::codec::RoleControlClient, 1));
    }
    // 9th: InstancesFull (bounded storage); slot never claimed by non-exclusive.
    env.send(req(200, 7, 5, v3::codec::RoleControlClient, 1));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::InstancesFull));
    EXPECT_FALSE(env.rt.slot_held());
    // Exclusive type when capacity is full: InstancesFull (capacity checked
    // BEFORE the slot claim - no partial reservation, #13).
    env.send(req(201, 7, 5, v3::codec::RoleControlClient, 2));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::InstancesFull));
    EXPECT_FALSE(env.rt.slot_held());
}

TEST(Semantic, HandshakeRequiredWithoutGrant)
{
    SemEnv env(/*grant_authority*/ 0); // no handshake: mutating forbidden (#47 section 5.1)
    env.register_type(1);
    env.send(req(1));
    EXPECT_EQ(env.last_reject(), static_cast<std::uint8_t>(RejectCode::HandshakeRequired));
    EXPECT_EQ(env.events.started_count, 0u);
}

TEST(Semantic, StopAndSubscribeRequireHandshake)
{
    SemEnv env(/*grant_authority*/ 0); // pre-handshake principal
    env.register_type(1);

    // Stop intent pre-handshake: rejected (HandshakeRequired), no mutation.
    env.send_stop(123);
    EXPECT_EQ(env.events.reject_count, 1u);
    EXPECT_EQ(env.events.rejects[0], static_cast<std::uint8_t>(RejectCode::HandshakeRequired));
    EXPECT_EQ(env.rt.active_count(), 0u);

    // Subscribe pre-handshake: rejected, no subscription created.
    env.send_subscribe(v3::codec::ClassBitTelemetry);
    EXPECT_EQ(env.events.reject_count, 2u);
    EXPECT_EQ(env.events.rejects[1], static_cast<std::uint8_t>(RejectCode::HandshakeRequired));
    EXPECT_EQ(env.subs.active_count(), 0u);
}

TEST(Semantic, StopBoundToOwningPrincipal)
{
    SemEnv env;
    env.register_type(1, false, true, v3::slot::Activity::Motion, 64, test::driver_cancel_on_stop);
    env.send(req(1));
    const std::uint32_t op = env.last_ack_pos();
    EXPECT_EQ(env.rt.authority_of(op), 5u);

    // A stop from ANOTHER principal (authority 6): Unauthorized, no mutation
    // (authority binding, #47 section 5.1; review MAJOR-2 fix).
    test::FakeEpoch epoch2;
    epoch2.set(7);
    test::FakeWindow window2;
    test::FakeHealth health2;
    test::FakeProvisioning prov2;
    test::RecordingEvents events2;
    test::RecordingOutbound out2;
    v3::subscription::Registry subs2;
    subs2.init(0, &events2);
    v3::semantic::TypeRegistry types2;
    v3::semantic::SemanticContract sc2;
    Grant g2;
    g2.authority_id = 6;
    g2.roles = v3::codec::RoleControlClient;
    sc2.init(&epoch2, &window2, &health2, &prov2, &events2, &out2, &env.rt, &subs2, &types2, g2);

    std::uint8_t payload[4] = {static_cast<std::uint8_t>(op & 0xFFu),
                               static_cast<std::uint8_t>((op >> 8) & 0xFFu),
                               static_cast<std::uint8_t>((op >> 16) & 0xFFu),
                               static_cast<std::uint8_t>((op >> 24) & 0xFFu)};
    std::uint8_t frame[v3::codec::Mtu];
    const std::uint16_t flen = test::make_control_frame(
        static_cast<std::uint8_t>(v3::codec::MsgControl::StopIntent), payload, 4, frame, sizeof(frame));
    ASSERT_GT(flen, 0u);
    const DecodeResult dr = v3::codec::decode(frame, flen);
    ASSERT_TRUE(dr.ok());
    sc2.process_frame(dr.frame);

    EXPECT_TRUE(env.rt.is_active(op)); // NOT stopped by the foreign principal
    EXPECT_EQ(events2.reject_count, 1u);
    EXPECT_EQ(events2.rejects[0], static_cast<std::uint8_t>(RejectCode::Unauthorized));

    // The OWNER principal stops it: silent, idempotent; Cancelled on advance.
    env.send_stop(op);
    EXPECT_EQ(env.events.reject_count, 0u); // no rejection on the owner path
    env.rt.advance(5000);
    EXPECT_FALSE(env.rt.is_active(op));
    EXPECT_EQ(env.events.terminal_count, 1u);
    EXPECT_EQ(env.events.terminal[0].code, 1u); // Cancelled
}

TEST(Semantic, UnknownControlMessageDroppedWithEvent)
{
    SemEnv env;
    env.register_type(1);
    // Unknown msgType within the (known) Control family: dropped, msgType-level
    // error event (#47 section 18 #1), no outbound, no mutation.
    env.send_frame(0x7F, nullptr, 0);
    EXPECT_EQ(env.out.count(), 0u);
    EXPECT_EQ(env.events.error_count, 1u);
    EXPECT_EQ(env.events.errors[0],
              static_cast<std::uint8_t>(v3::codec::TransportError::UnknownMessage));
    EXPECT_EQ(env.events.started_count, 0u);
}

} // namespace
