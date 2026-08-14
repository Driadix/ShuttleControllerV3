// Contract core: canonical binary framing + typed message codecs + layered
// registries (design docs/operation-runtime-design-v3.md sections 2.1-2.3;
// normative #47 sections 2, 4, 7, 16; ticket #74). Pure module: no Arduino,
// no allocation (R1), every wire read bounds-checked (wire length is never
// trusted). Shared by BOTH transport profiles - the codec is not a
// transport-specific dialect (acceptance #74); transport adapters (#75) own
// link framing (E22 wrapper, partial frames, byte budgets) and call into it.
#pragma once

#include <cstdint>

namespace v3
{
namespace codec
{

// --- Layered registries (design section 2.2, #47 section 16) -----------------

enum class Family : std::uint8_t
{
    Handshake = 1,
    Control = 2,
    Service = 3,
    Update = 4,
    Session = 5,
    Observability = 6,
    Outcome = 7,
};

// Explicit queueClass on the wire (#47 section 8) - not derived from family.
enum class QueueClass : std::uint8_t
{
    Control = 0,
    Service = 1,
    Update = 2,
    Telemetry = 3,
    Events = 4,
    Logs = 5,
    Traces = 6,
};

// Frame/parse-level errors (#47 section 16.1).
enum class TransportError : std::uint8_t
{
    None = 0,
    BadSync = 1,
    BadCrc = 2,
    Truncated = 3,
    PayloadTooLong = 4,
    UnsupportedMajor = 5,
    UnknownFamily = 6,
    UnknownMessage = 7,  // msgType not in the family registry (additive)
    EncodeCapacity = 8,
};

// AdmissionRejectionCode - stable, additive (#47 section 16.2; design
// section 2.2 adds UnknownOperationType/InstancesFull/CompositionInvalid
// under the extensibility clause).
enum class RejectCode : std::uint8_t
{
    EpochMismatch = 0,
    HandshakeRequired = 1,
    Unauthorized = 2,
    RoleEscalation = 3,
    UnsupportedVersion = 4,
    UnknownCapabilityRequired = 5,
    InvalidEnvelope = 6,
    InvalidParameters = 7,
    Conflict = 8,
    ResourceConflict = 9,
    WrongWindow = 10,
    HealthGate = 11,
    ProvisioningGate = 12,
    ProfileDenied = 13,
    ProfileMismatch = 14,
    ProfileNotQualified = 15,
    SequenceStale = 16,
    BusyRejected = 17,
    UnknownOperationType = 18,
    InstancesFull = 19,
    CompositionInvalid = 20,
    UnknownSubscription = 21, // unsubscribe of an id not owned by the principal
};

// OperationOutcomeCode - common families (#13, #47 section 16.3); per-type
// codes are added by operation-type contracts (Phase 3), additively.
enum class OutcomeCode : std::uint16_t
{
    Succeeded = 0,
    Cancelled = 1,
    FailedGeneric = 2,
};

// Message types per family (codecs in scope #74, design section 2.2).
enum class MsgControl : std::uint8_t
{
    OperationRequest = 0,
    AdmissionAckPositive = 1,
    AdmissionAckNegative = 2,
    StopIntent = 3,
    Query = 4,
    Subscribe = 5,
    Unsubscribe = 6,
    SubscriptionAck = 7,
};

enum class MsgHandshake : std::uint8_t
{
    Hello = 0,
    HelloAck = 1,
    HandshakeReject = 2,
};

// Roles (canonical CONTEXT.md; bitmask within a grant).
constexpr std::uint8_t RoleControlClient = 0x01;
constexpr std::uint8_t RoleServiceClient = 0x02;
constexpr std::uint8_t RoleSafetyAuthority = 0x04;

// --- Canonical frame (design section 2.1, #47 section 4.1) -------------------

// Versioned framing markers (schema table #47 section 4.1; not required to
// match V1 0xBB 0xCC).
constexpr std::uint8_t Sync0 = 0xE3;
constexpr std::uint8_t Sync1 = 0x10;
constexpr std::uint8_t ProtocolMajor = 1;

constexpr std::uint16_t Mtu = 128;        // #48 section 6 (both profiles)
constexpr std::uint16_t MaxPayload = 116; // MTU - (sync 2 + header 8 + crc 2)
constexpr std::uint16_t HeaderSize = 8;
constexpr std::uint16_t CrcSize = 2;

// Header flags: reserve slot (stop/handshake never rejected, #43 section 6).
constexpr std::uint8_t FlagReserve = 0x01;

#pragma pack(push, 1)
struct Header
{
    std::uint8_t protocol_major = ProtocolMajor;
    std::uint8_t msg_family = 0;
    std::uint8_t msg_type = 0;
    std::uint8_t queue_class = 0;
    std::uint8_t flags = 0;
    std::uint8_t frame_seq = 0; // per-link transport plane (#47 section 4.3)
    std::uint16_t payload_len = 0; // little-endian
};
#pragma pack(pop)

struct DecodedFrame
{
    Header header;
    const std::uint8_t* payload = nullptr; // view into the caller buffer (no copy)
    std::uint16_t payload_len = 0;
};

struct DecodeResult
{
    TransportError error = TransportError::None;
    DecodedFrame frame;
    bool ok() const { return error == TransportError::None; }
};

// Decodes one canonical frame from buf (len bytes). Bounds-checked: never
// reads past buf+len; wire lengths are never trusted (R1/R4). payload is a
// view into buf - valid while buf is alive.
DecodeResult decode(const std::uint8_t* buf, std::uint16_t len);

// Encodes sync+header+payload+crc16 into buf (cap bytes). Returns total frame
// length on success, 0 on capacity error - no partial write beyond cap.
std::uint16_t encode(std::uint8_t* buf, std::uint16_t cap, const Header& h,
                     const std::uint8_t* payload, std::uint16_t payload_len);

// CRC-16/CCITT-FALSE over data: integrity against ACCIDENTAL corruption only,
// not authenticity or MAC (#47 section 6).
std::uint16_t crc16(const std::uint8_t* data, std::uint16_t len);

// --- Typed messages (design section 2.3) -------------------------------------

constexpr std::uint16_t MaxParams = 64; // bounded opaque parameters blob

#pragma pack(push, 1)
struct OperationRequest
{
    std::uint32_t request_id = 0;         // unique per (controllerEpoch, authorityId)
    std::uint32_t controller_epoch = 0;   // fencing boundary (#13)
    std::uint16_t authority_id = 0;       // echo only - never the principal resolver (#47 section 5.1)
    std::uint8_t role = 0;                // must be within the handshake grant
    std::uint16_t operation_type = 0;
    std::uint32_t parent_operation_id = 0; // 0 = root
    std::uint8_t params_len = 0;           // <= MaxParams
    std::uint8_t params[MaxParams] = {};
};

struct AdmissionAckPositive
{
    std::uint32_t request_id = 0;
    std::uint32_t controller_epoch = 0;
    std::uint32_t operation_id = 0; // controller-authored after accept (#13)
    std::uint16_t operation_type = 0;
    std::uint32_t parent_operation_id = 0; // 0 for root
};

struct AdmissionAckNegative // NEVER carries operationId (#47 section 18 #5)
{
    std::uint32_t request_id = 0;
    std::uint32_t controller_epoch = 0;
    std::uint8_t reject_code = 0;
};

struct StopIntent
{
    std::uint32_t operation_id = 0;
};

struct Query
{
    std::uint8_t sections_mask = 0;
};

struct Subscribe
{
    std::uint8_t class_mask = 0;       // telemetry|events|logs|traces bits
    std::uint8_t filter = 0;           // opaque extension point (#49 section 9)
    std::uint16_t min_interval_ms = 0; // 0 = profile default
    std::uint16_t max_bytes_per_tick = 0;
};

struct Unsubscribe
{
    std::uint8_t sub_id = 0;
};

struct SubscriptionAck
{
    std::uint8_t sub_id = 0;
    bool accepted = false;
    std::uint8_t reject_code = 0; // RejectCode when rejected
};
#pragma pack(pop)

// Class-mask bits for Subscribe::class_mask (mirrors QueueClass egress classes).
constexpr std::uint8_t ClassBitTelemetry = 0x01;
constexpr std::uint8_t ClassBitEvents = 0x02;
constexpr std::uint8_t ClassBitLogs = 0x04;
constexpr std::uint8_t ClassBitTraces = 0x08;

enum class CodecResult : std::uint8_t
{
    Ok = 0,
    Truncated = 1,   // payload shorter than the fixed layout
    OutOfBounds = 2, // params_len exceeds MaxParams, or trailing bytes
};

// --- Typed codecs (bounds-checked, little-endian; typed outcomes, R5) --------

CodecResult decode_operation_request(const std::uint8_t* p, std::uint16_t len, OperationRequest& out);
CodecResult decode_stop_intent(const std::uint8_t* p, std::uint16_t len, StopIntent& out);
CodecResult decode_query(const std::uint8_t* p, std::uint16_t len, Query& out);
CodecResult decode_subscribe(const std::uint8_t* p, std::uint16_t len, Subscribe& out);
CodecResult decode_unsubscribe(const std::uint8_t* p, std::uint16_t len, Unsubscribe& out);

// Encode into buf (cap); returns bytes written, 0 on capacity error.
std::uint16_t encode_ack_pos(std::uint8_t* buf, std::uint16_t cap, const AdmissionAckPositive& a);
std::uint16_t encode_ack_neg(std::uint8_t* buf, std::uint16_t cap, const AdmissionAckNegative& a);
std::uint16_t encode_sub_ack(std::uint8_t* buf, std::uint16_t cap, const SubscriptionAck& a);

// Symmetric decoders (used by tests and the transport adapter #75).
CodecResult decode_ack_pos(const std::uint8_t* p, std::uint16_t len, AdmissionAckPositive& out);
CodecResult decode_ack_neg(const std::uint8_t* p, std::uint16_t len, AdmissionAckNegative& out);
CodecResult decode_sub_ack(const std::uint8_t* p, std::uint16_t len, SubscriptionAck& out);

// CRC-32 (IEEE) over the canonical identity+payload fields
// (role, operation_type, parent_operation_id, params) - idempotency
// fingerprint (design section 2.3; #13 conflict semantics).
std::uint32_t request_fingerprint(const OperationRequest& r);

// --- Observability records (design docs/observability-design-v3.md section 2;
//     architecture #49 sections 2, 5, 8; ticket #72) ------------------------

// Message types in the Observability family (additive extension of #47 section 7).
enum class MsgObservability : std::uint8_t
{
    TelemetryRecord = 0,
    EventRecord = 1,
    LogRecord = 2,
    TraceRecord = 3,
    SnapshotFragment = 4,
};

// Wall-clock quality (#49 section 3): Unsynced => wall_time absent (0) and not
// to be interpreted. SetWallClock (Service class) is outside #72.
enum class TimeValidity : std::uint8_t
{
    Unsynced = 0,
    RtcOnly = 1,
    Synced = 2,
};

// Common envelope: first field group of every class record. Fixed per-class
// layout (NOT dynamic): telemetry 10 B (no wall_time - ordering by tick, #49
// section 2.2), events/logs/traces 14 B (wall_time present; 0 when Unsynced).
// LSB-first bit packing (R7: explicit contract; wire is little-endian like
// the whole frame). class_id is 3 bits - QueueClass 0..6 (Events=4 requires
// 3 bits; #49 section 2.1 says "2 bits significant" - additive widening,
// wire-compatible u8 with 5 high bits reserved/0).
struct Envelope
{
    std::uint8_t class_id = 0;        // codec::QueueClass: Control..Traces (0..6)
    TimeValidity time_validity = TimeValidity::Unsynced;
    std::uint32_t controller_epoch = 0;
    std::uint32_t monotonic_tick = 0; // ms from boot; sole order authority (#43, #48 s9)
    std::uint8_t seq = 0;             // rolling per-class; loss detection mod 256
    std::uint32_t wall_time = 0;      // epoch sec; valid only when time_validity != Unsynced

    // Wire sizes.
    static constexpr std::uint16_t SizeNoWall = 10;
    static constexpr std::uint16_t SizeWithWall = 14;
};

// telemetry body (drop-oldest, freshness #49 section 2.2). 18 B packed.
struct TelemetryBody
{
    std::uint8_t op_state = 0;
    std::uint32_t position_mm = 0;
    std::uint16_t speed_mm_s = 0;
    std::uint8_t health = 0;          // safety::SafetyHealth
    std::uint16_t fault_mask = 0;     // #47 section 16.4
    std::uint16_t warning_mask = 0;
    std::uint8_t battery_charge = 0;
    std::uint16_t battery_voltage_mv = 0;
    std::uint16_t pallet_count = 0;
    std::uint8_t state_flags = 0;
};

// events body (drop-newest, reserved capacity #43 section 6). 12 B packed.
struct EventBody
{
    std::uint16_t event_id = 0; // registry 0x01xx..0x08xx (#49 section 5)
    std::uint8_t severity = 0;  // info/warning/error/fatal
    std::uint8_t ctx_kind = 0;  // operationId/faultCode/dropCounter/rejectCode/updateStage...
    std::uint32_t ctx_value = 0;
    std::uint32_t ctx_value2 = 0;
};

// logs body (drop-newest; text <= 80 B after envelope, no chunk split).
struct LogBody
{
    std::uint8_t level = 0;    // DEBUG/INFO/WARN/ERROR/FATAL
    std::uint8_t module_id = 0;
    std::uint8_t text[80] = {}; // bounded, truncated without chunk split
    std::uint16_t text_len = 0;
};

// traces body header; body payload follows (fault-capture staging <= 512 B).
struct TraceBodyHeader
{
    std::uint8_t kind = 0;          // 0 = fault_capture (production)
    std::uint16_t trigger_event_id = 0;
    std::uint32_t trigger_tick = 0;
    std::uint32_t payload_len = 0;  // capture body length
};

// Envelope codec. with_wall selects the fixed layout (10/14 B).
std::uint16_t encode_envelope(std::uint8_t* buf, std::uint16_t cap, const Envelope& e, bool with_wall);
CodecResult decode_envelope(const std::uint8_t* p, std::uint16_t len, Envelope& out, bool with_wall);

// Class-body codecs (LE, bounds-checked).
std::uint16_t encode_telemetry_body(std::uint8_t* buf, std::uint16_t cap, const TelemetryBody& b);
CodecResult decode_telemetry_body(const std::uint8_t* p, std::uint16_t len, TelemetryBody& out);
std::uint16_t encode_event_body(std::uint8_t* buf, std::uint16_t cap, const EventBody& b);
CodecResult decode_event_body(const std::uint8_t* p, std::uint16_t len, EventBody& out);
std::uint16_t encode_log_body(std::uint8_t* buf, std::uint16_t cap, const LogBody& b);
CodecResult decode_log_body(const std::uint8_t* p, std::uint16_t len, LogBody& out);
std::uint16_t encode_trace_header(std::uint8_t* buf, std::uint16_t cap, const TraceBodyHeader& h);
CodecResult decode_trace_header(const std::uint8_t* p, std::uint16_t len, TraceBodyHeader& out);

} // namespace codec
} // namespace v3
