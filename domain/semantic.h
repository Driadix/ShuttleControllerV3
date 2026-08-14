// Semantic Contract & Admission (design docs/operation-runtime-design-v3.md
// sections 2.4, 3.2; #13 admission steps 1-7; #46 section 8 admission matrix;
// ticket #74). Framework-free. One decoded control-class frame per call
// (bounded, <= T_step); responses are canonical frames on the outbound port.
//
// Admission order is STRICTLY #13 (owner decision #74 section 0.4): the
// idempotency/conflict check is normative step 5 - AFTER envelope/epoch/
// authority, parameters schema, preconditions and composition - never an
// early bypass. On a same-result hit the stored result is replayed WITHOUT
// re-reserving resources (steps 6-7 skipped), even if dynamic gates
// (window/health/provisioning/slot) changed since.
#pragma once

#include <cstdint>

#include "domain/codec.h"
#include "domain/ledger.h"
#include "domain/ports.h"
#include "domain/runtime.h"
#include "domain/subscriptions.h"

namespace v3
{
namespace semantic
{

// Operation-type contract (Phase 3 supplies real types). #74 ships the
// structure with an EMPTY registry: admission of an unknown type answers
// UnknownOperationType. Tests inject synthetic types.
struct OperationType
{
    std::uint16_t id = 0;
    bool root_allowed = true;
    bool child_allowed = false;
    bool exclusive = false;                              // takes the exclusive slot (I-LC-4)
    slot::Activity activity = slot::Activity::Idle;      // exclusive class for the slot
    std::uint8_t params_max = codec::MaxParams;
    runtime::DriverFn driver = nullptr;
};

class TypeRegistry
{
  public:
    static constexpr std::uint32_t Capacity = 32; // bounded (R4)

    const OperationType* find(std::uint16_t id) const;
    bool register_type(const OperationType& t); // false: full or duplicate id

  private:
    OperationType m_types[Capacity];
    std::uint8_t m_count = 0;
};

// Handshake grant of the current principal (#47 section 5.1). #75 supplies
// the full principal registry (bridgePrincipalHandle -> authorityId); #74
// consumes a single grant (host tests inject; production wiring provides it
// after the handshake machine lands).
struct Grant
{
    std::uint16_t authority_id = 0; // 0 = no handshake -> HandshakeRequired
    std::uint8_t roles = 0;         // bitmask (codec::Role*)
    std::uint8_t capabilities = 0;  // reserved for capability gates (#47)
};

struct AdmitOutcome
{
    bool accepted = false;
    bool replay = false;                    // idempotency hit: stored result replayed
    std::uint32_t operation_id = 0;         // accepted
    codec::RejectCode reject = codec::RejectCode::UnknownOperationType; // rejected
};

class SemanticContract
{
  public:
    void init(EpochSource* epoch, WindowSource* window, HealthSource* health,
              ProvisioningSource* provisioning, RuntimeEvents* events,
              OutboundControl* outbound, runtime::Runtime* rt,
              subscription::Registry* subs, TypeRegistry* types, const Grant& grant);

    // One decoded frame (the glue decodes; foreground-only; bounded).
    // Frames outside the Control family are not handled in #74 (their
    // payload codecs land with #75/#76/#77).
    void process_frame(const codec::DecodedFrame& f);

  private:
    void handle_operation_request(const codec::DecodedFrame& f);
    void handle_stop_intent(const codec::DecodedFrame& f);
    void handle_query(const codec::DecodedFrame& f);
    void handle_subscribe(const codec::DecodedFrame& f);
    void handle_unsubscribe(const codec::DecodedFrame& f);

    // Full pipeline (#13 steps 1-7, design 3.2).
    AdmitOutcome admit(const codec::OperationRequest& r);
    bool role_allowed(std::uint8_t role) const;
    void store_reject(const codec::OperationRequest& r, codec::RejectCode code);
    void send_control(std::uint8_t msg_type, const std::uint8_t* payload, std::uint16_t len);
    void send_ack_pos(const codec::AdmissionAckPositive& a);
    void send_ack_neg(const codec::AdmissionAckNegative& a);

    EpochSource* m_epoch = nullptr;
    WindowSource* m_window = nullptr;
    HealthSource* m_health = nullptr;
    ProvisioningSource* m_prov = nullptr;
    RuntimeEvents* m_events = nullptr;
    OutboundControl* m_out = nullptr;
    runtime::Runtime* m_rt = nullptr;
    subscription::Registry* m_subs = nullptr;
    TypeRegistry* m_types = nullptr;
    Grant m_grant;
    IdempotencyLedger m_ledger;
};

} // namespace semantic
} // namespace v3
