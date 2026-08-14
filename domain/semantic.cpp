// Semantic Contract & Admission implementation (design
// docs/operation-runtime-design-v3.md section 3.2; #13; #46 section 8;
// ticket #74).
#include "domain/semantic.h"

namespace v3
{
namespace semantic
{

const OperationType* TypeRegistry::find(std::uint16_t id) const
{
    for (std::uint8_t i = 0; i < m_count; ++i)
    {
        if (m_types[i].id == id)
        {
            return &m_types[i];
        }
    }
    return nullptr;
}

bool TypeRegistry::register_type(const OperationType& t)
{
    if (find(t.id) != nullptr || m_count >= Capacity)
    {
        return false;
    }
    m_types[m_count++] = t;
    return true;
}

void SemanticContract::init(EpochSource* epoch, WindowSource* window, HealthSource* health,
                            ProvisioningSource* provisioning, RuntimeEvents* events,
                            OutboundControl* outbound, runtime::Runtime* rt,
                            subscription::Registry* subs, TypeRegistry* types, const Grant& grant)
{
    m_epoch = epoch;
    m_window = window;
    m_health = health;
    m_prov = provisioning;
    m_events = events;
    m_out = outbound;
    m_rt = rt;
    m_subs = subs;
    m_types = types;
    m_grant = grant;
}

bool SemanticContract::role_allowed(std::uint8_t role) const
{
    if (role == 0)
    {
        return false;
    }
    return (m_grant.roles & role) == role; // claimed role must be within the grant (#47 §18 #13)
}

void SemanticContract::process_frame(const codec::DecodedFrame& f)
{
    if (f.header.msg_family != static_cast<std::uint8_t>(codec::Family::Control))
    {
        // Handshake/Service/Update/Session payload codecs land with #75/#76/#77.
        return;
    }
    switch (static_cast<codec::MsgControl>(f.header.msg_type))
    {
    case codec::MsgControl::OperationRequest:
        handle_operation_request(f);
        break;
    case codec::MsgControl::StopIntent:
        handle_stop_intent(f);
        break;
    case codec::MsgControl::Query:
        handle_query(f);
        break;
    case codec::MsgControl::Subscribe:
        handle_subscribe(f);
        break;
    case codec::MsgControl::Unsubscribe:
        handle_unsubscribe(f);
        break;
    default:
        // Unknown or not-yet-implemented control messages: drop + event
        // (no silent acceptance, R5).
        if (m_events != nullptr)
        {
            m_events->transport_error(codec::TransportError::UnknownFamily);
        }
        break;
    }
}

void SemanticContract::handle_operation_request(const codec::DecodedFrame& f)
{
    codec::OperationRequest req;
    if (codec::decode_operation_request(f.payload, f.payload_len, req) != codec::CodecResult::Ok)
    {
        // Malformed payload: drop + event (no requestId -> no negative ACK).
        if (m_events != nullptr)
        {
            m_events->transport_error(codec::TransportError::Truncated);
        }
        return;
    }

    const AdmitOutcome out = admit(req);
    if (out.replay && m_events != nullptr)
    {
        m_events->request_duplicate(req.request_id, false); // stored result replayed (#13)
    }
    if (out.accepted)
    {
        codec::AdmissionAckPositive ack;
        ack.request_id = req.request_id;
        ack.controller_epoch = m_epoch->epoch();
        ack.operation_id = out.operation_id;
        ack.operation_type = req.operation_type;
        ack.parent_operation_id = req.parent_operation_id;
        send_ack_pos(ack);
        return;
    }

    if (!out.replay && m_events != nullptr)
    {
        m_events->admission_rejected(static_cast<std::uint8_t>(out.reject));
    }

    codec::AdmissionAckNegative ack;
    ack.request_id = req.request_id;
    ack.controller_epoch = m_epoch->epoch();
    ack.reject_code = static_cast<std::uint8_t>(out.reject); // never carries operationId (#47 §18 #5)
    send_ack_neg(ack);
}

AdmitOutcome SemanticContract::admit(const codec::OperationRequest& r)
{
    AdmitOutcome out;

    // Step 1: envelope/epoch/authority/rights (#13).
    if (m_epoch == nullptr || r.controller_epoch != m_epoch->epoch())
    {
        out.reject = codec::RejectCode::EpochMismatch;
        return out;
    }
    if (m_grant.authority_id == 0)
    {
        out.reject = codec::RejectCode::HandshakeRequired; // mutating requires handshake (#47 §5.1)
        return out;
    }
    if (r.authority_id != m_grant.authority_id)
    {
        out.reject = codec::RejectCode::Unauthorized; // echo mismatch: client cannot act as another principal
        return out;
    }
    if (!role_allowed(r.role))
    {
        out.reject = codec::RejectCode::RoleEscalation; // claimed role outside the grant (#47 §18 #13)
        return out;
    }

    // Steps 2-4: type, parameters schema, static composition (deterministic
    // for a given payload - a replay therefore reaches the lookup identically).
    const OperationType* t = m_types->find(r.operation_type);
    if (t == nullptr)
    {
        out.reject = codec::RejectCode::UnknownOperationType;
        return out;
    }
    if (r.parent_operation_id == 0 && !t->root_allowed)
    {
        out.reject = codec::RejectCode::CompositionInvalid;
        return out;
    }
    if (r.params_len > t->params_max)
    {
        out.reject = codec::RejectCode::InvalidParameters;
        return out;
    }
    if (r.parent_operation_id != 0 && !t->child_allowed)
    {
        out.reject = codec::RejectCode::CompositionInvalid; // edge not allowed by the type graph (#13)
        return out;
    }

    // Step 5: idempotency/conflict (normative order, #13; owner decision §0.4).
    const std::uint32_t fp = codec::request_fingerprint(r);
    LedgerEntry stored;
    switch (m_ledger.lookup(m_epoch->epoch(), m_grant.authority_id, r.request_id, fp, stored))
    {
    case IdempotencyLedger::Lookup::SameResult:
        // Replay: return the stored result WITHOUT re-reserving resources
        // (steps 6-7 skipped) - same result despite changed gates (#13).
        out.replay = true;
        out.accepted = stored.kind == 0;
        out.operation_id = out.accepted ? stored.outcome : 0;
        out.reject = out.accepted ? codec::RejectCode::UnknownOperationType
                                  : static_cast<codec::RejectCode>(stored.outcome);
        return out;
    case IdempotencyLedger::Lookup::Conflict:
        out.reject = codec::RejectCode::Conflict; // same key, different payload: no instance (#13)
        return out;
    case IdempotencyLedger::Lookup::Miss:
        break;
    }

    // Step 6: resource availability and exclusivity (#46 section 8 matrix).
    if (r.parent_operation_id == 0)
    {
        if (t->exclusive)
        {
            if (m_window != nullptr && m_window->window() != PlatformWindow::Serving)
            {
                store_reject(r, codec::RejectCode::WrongWindow);
                out.reject = codec::RejectCode::WrongWindow;
                return out;
            }
            if (m_health != nullptr && m_health->health() == safety::SafetyHealth::Fault)
            {
                store_reject(r, codec::RejectCode::HealthGate);
                out.reject = codec::RejectCode::HealthGate;
                return out;
            }
            if (m_prov != nullptr && m_prov->status() != ProvisioningStatus::Provisioned)
            {
                store_reject(r, codec::RejectCode::ProvisioningGate);
                out.reject = codec::RejectCode::ProvisioningGate;
                return out;
            }
            if (m_rt != nullptr && m_rt->slot_held())
            {
                store_reject(r, codec::RejectCode::ResourceConflict);
                out.reject = codec::RejectCode::ResourceConflict;
                return out;
            }
        }
        else
        {
            // Non-exclusive root: read-only-like surface (Serving/Update/Recovery).
            if (m_window != nullptr && m_window->window() == PlatformWindow::Boot)
            {
                store_reject(r, codec::RejectCode::WrongWindow);
                out.reject = codec::RejectCode::WrongWindow;
                return out;
            }
        }
    }
    else if (m_rt != nullptr && !m_rt->is_active(r.parent_operation_id))
    {
        store_reject(r, codec::RejectCode::ResourceConflict); // delegation unavailable (#13)
        out.reject = codec::RejectCode::ResourceConflict;
        return out;
    }

    // Step 7: create the instance and fix the initial lifecycle.
    runtime::CreateRequest cr;
    cr.type_id = r.operation_type;
    cr.authority_id = m_grant.authority_id; // RESOLVED, not the client echo (#47 §18 #12)
    cr.parent_op_id = r.parent_operation_id;
    cr.role = r.role;
    cr.params_len = r.params_len;
    for (std::uint8_t i = 0; i < r.params_len; ++i)
    {
        cr.params[i] = r.params[i];
    }
    cr.fn = t->driver;
    cr.ctx = nullptr;
    cr.exclusive_class = t->exclusive ? t->activity : slot::Activity::Idle;

    std::uint32_t op_id = 0;
    const runtime::Runtime::CreateResult res =
        r.parent_operation_id == 0 ? m_rt->create_root(cr, op_id) : m_rt->create_child(cr, op_id);
    switch (res)
    {
    case runtime::Runtime::CreateResult::Accepted:
        break;
    case runtime::Runtime::CreateResult::InstancesFull:
        store_reject(r, codec::RejectCode::InstancesFull);
        out.reject = codec::RejectCode::InstancesFull; // bounded storage (design 8)
        return out;
    case runtime::Runtime::CreateResult::ExclusiveBusy:
        store_reject(r, codec::RejectCode::ResourceConflict);
        out.reject = codec::RejectCode::ResourceConflict;
        return out;
    case runtime::Runtime::CreateResult::ParentMissing:
        store_reject(r, codec::RejectCode::ResourceConflict);
        out.reject = codec::RejectCode::ResourceConflict;
        return out;
    default: // TreeCycle / DepthExceeded / EdgeDenied
        store_reject(r, codec::RejectCode::CompositionInvalid);
        out.reject = codec::RejectCode::CompositionInvalid;
        return out;
    }

    // Ledger stores the accepted outcome; the initial lifecycle is Accepted.
    LedgerEntry e;
    e.request_id = r.request_id;
    e.fingerprint = fp;
    e.outcome = op_id;
    e.kind = 0; // accepted
    m_ledger.store(m_epoch->epoch(), m_grant.authority_id, e);

    out.accepted = true;
    out.operation_id = op_id;
    return out;
}

void SemanticContract::store_reject(const codec::OperationRequest& r, codec::RejectCode code)
{
    // Rejected outcomes are stored too: a replay after gates changed must
    // return the SAME rejection (#13 «тот же admission result»; design §2.4).
    LedgerEntry e;
    e.request_id = r.request_id;
    e.fingerprint = codec::request_fingerprint(r);
    e.outcome = static_cast<std::uint32_t>(code);
    e.kind = 1; // rejected
    m_ledger.store(m_epoch->epoch(), m_grant.authority_id, e);
}

void SemanticContract::handle_stop_intent(const codec::DecodedFrame& f)
{
    codec::StopIntent s;
    if (codec::decode_stop_intent(f.payload, f.payload_len, s) != codec::CodecResult::Ok)
    {
        if (m_events != nullptr)
        {
            m_events->transport_error(codec::TransportError::Truncated);
        }
        return;
    }
    // Stop is a control intent, idempotent; no ACK required (#13, #47 Q25).
    (void)m_rt->stop(s.operation_id);
}

void SemanticContract::handle_query(const codec::DecodedFrame& f)
{
    codec::Query q;
    if (codec::decode_query(f.payload, f.payload_len, q) != codec::CodecResult::Ok)
    {
        if (m_events != nullptr)
        {
            m_events->transport_error(codec::TransportError::Truncated);
        }
        return;
    }
    // #46 section 8: query/read-only - Serving/Update/Recovery (not Boot),
    // any health, no exclusive slot.
    if (m_window != nullptr && m_window->window() == PlatformWindow::Boot)
    {
        if (m_events != nullptr)
        {
            m_events->admission_rejected(static_cast<std::uint8_t>(codec::RejectCode::WrongWindow));
        }
        return;
    }
    // Forward the query to the outbound control path as a canonical frame;
    // the snapshot provider (#72 Producer) assembles the document and
    // answers (design section 3.2, #49 section 2.6).
    std::uint8_t frame[codec::Mtu];
    codec::Header h;
    h.msg_family = static_cast<std::uint8_t>(codec::Family::Control);
    h.msg_type = static_cast<std::uint8_t>(codec::MsgControl::Query);
    h.queue_class = static_cast<std::uint8_t>(codec::QueueClass::Control);
    const std::uint16_t flen = codec::encode(frame, sizeof(frame), h, f.payload, f.payload_len);
    if (flen > 0 && m_out != nullptr)
    {
        (void)m_out->enqueue(codec::QueueClass::Control, frame, flen);
    }
}

void SemanticContract::handle_subscribe(const codec::DecodedFrame& f)
{
    codec::Subscribe s;
    if (codec::decode_subscribe(f.payload, f.payload_len, s) != codec::CodecResult::Ok)
    {
        if (m_events != nullptr)
        {
            m_events->transport_error(codec::TransportError::Truncated);
        }
        return;
    }
    codec::SubscriptionAck ack;
    std::uint8_t sub_id = 0;
    const subscription::Registry::Result res = m_subs->subscribe(m_grant.authority_id, s, sub_id);
    ack.sub_id = sub_id;
    ack.accepted = res == subscription::Registry::Result::Ok;
    ack.reject_code = res == subscription::Registry::Result::Ok
                          ? 0
                          : static_cast<std::uint8_t>(codec::RejectCode::BusyRejected); // caps exhausted
    std::uint8_t payload[codec::MaxPayload];
    const std::uint16_t plen = codec::encode_sub_ack(payload, sizeof(payload), ack);
    if (plen > 0)
    {
        send_control(static_cast<std::uint8_t>(codec::MsgControl::SubscriptionAck), payload, plen);
    }
}

void SemanticContract::handle_unsubscribe(const codec::DecodedFrame& f)
{
    codec::Unsubscribe u;
    if (codec::decode_unsubscribe(f.payload, f.payload_len, u) != codec::CodecResult::Ok)
    {
        if (m_events != nullptr)
        {
            m_events->transport_error(codec::TransportError::Truncated);
        }
        return;
    }
    codec::SubscriptionAck ack;
    const subscription::Registry::Result res = m_subs->unsubscribe(m_grant.authority_id, u.sub_id);
    ack.sub_id = u.sub_id;
    ack.accepted = res == subscription::Registry::Result::Ok;
    std::uint8_t payload[codec::MaxPayload];
    const std::uint16_t plen = codec::encode_sub_ack(payload, sizeof(payload), ack);
    if (plen > 0)
    {
        send_control(static_cast<std::uint8_t>(codec::MsgControl::SubscriptionAck), payload, plen);
    }
}

void SemanticContract::send_ack_pos(const codec::AdmissionAckPositive& a)
{
    std::uint8_t payload[codec::MaxPayload];
    const std::uint16_t plen = codec::encode_ack_pos(payload, sizeof(payload), a);
    if (plen > 0)
    {
        send_control(static_cast<std::uint8_t>(codec::MsgControl::AdmissionAckPositive), payload, plen);
    }
}

void SemanticContract::send_ack_neg(const codec::AdmissionAckNegative& a)
{
    std::uint8_t payload[codec::MaxPayload];
    const std::uint16_t plen = codec::encode_ack_neg(payload, sizeof(payload), a);
    if (plen > 0)
    {
        send_control(static_cast<std::uint8_t>(codec::MsgControl::AdmissionAckNegative), payload, plen);
    }
}

void SemanticContract::send_control(std::uint8_t msg_type, const std::uint8_t* payload, std::uint16_t len)
{
    std::uint8_t frame[codec::Mtu];
    codec::Header h;
    h.msg_family = static_cast<std::uint8_t>(codec::Family::Control);
    h.msg_type = msg_type;
    h.queue_class = static_cast<std::uint8_t>(codec::QueueClass::Control);
    const std::uint16_t flen = codec::encode(frame, sizeof(frame), h, payload, len);
    if (flen > 0 && m_out != nullptr)
    {
        (void)m_out->enqueue(codec::QueueClass::Control, frame, flen);
    }
}

} // namespace semantic
} // namespace v3
