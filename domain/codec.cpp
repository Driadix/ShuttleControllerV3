// Contract core implementation (design docs/operation-runtime-design-v3.md
// sections 2.1-2.3; ticket #74). Explicit little-endian field access - wire
// memory is never cast to structs (alignment/aliasing/endianness safe).
#include "domain/codec.h"

namespace
{

inline std::uint16_t rd16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                      (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t rd32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline void wr16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

inline void wr32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

inline bool family_known(std::uint8_t family)
{
    return family >= 1 && family <= 7;
}

} // namespace

namespace v3
{
namespace codec
{

std::uint16_t crc16(const std::uint8_t* data, std::uint16_t len)
{
    std::uint16_t crc = 0xFFFFu;
    for (std::uint16_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[i]) << 8);
        for (std::uint8_t b = 0; b < 8; ++b)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021u);
            }
            else
            {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

DecodeResult decode(const std::uint8_t* buf, std::uint16_t len)
{
    DecodeResult result;
    constexpr std::uint16_t kMinFrame = 12; // sync 2 + header 8 + crc 2
    if (len < kMinFrame)
    {
        result.error = TransportError::Truncated;
        return result;
    }
    if (buf[0] != Sync0 || buf[1] != Sync1)
    {
        result.error = TransportError::BadSync;
        return result;
    }

    Header& h = result.frame.header;
    h.protocol_major = buf[2];
    h.msg_family = buf[3];
    h.msg_type = buf[4];
    h.queue_class = buf[5];
    h.flags = buf[6];
    h.frame_seq = buf[7];
    h.payload_len = rd16(buf + 8);

    if (h.payload_len > MaxPayload)
    {
        result.error = TransportError::PayloadTooLong;
        return result;
    }
    const std::uint16_t total = static_cast<std::uint16_t>(kMinFrame + h.payload_len);
    if (total > len)
    {
        result.error = TransportError::Truncated;
        return result;
    }
    if (rd16(buf + 10 + h.payload_len) != crc16(buf + 2, static_cast<std::uint16_t>(8 + h.payload_len)))
    {
        result.error = TransportError::BadCrc;
        return result;
    }
    if (h.protocol_major != ProtocolMajor)
    {
        result.error = TransportError::UnsupportedMajor;
        return result;
    }
    if (!family_known(h.msg_family))
    {
        result.error = TransportError::UnknownFamily;
        return result;
    }

    result.frame.payload = buf + 10;
    result.frame.payload_len = h.payload_len;
    result.error = TransportError::None;
    return result;
}

std::uint16_t encode(std::uint8_t* buf, std::uint16_t cap, const Header& h,
                     const std::uint8_t* payload, std::uint16_t payload_len)
{
    if (payload_len > MaxPayload)
    {
        return 0;
    }
    const std::uint16_t total = static_cast<std::uint16_t>(12 + payload_len);
    if (cap < total)
    {
        return 0;
    }
    buf[0] = Sync0;
    buf[1] = Sync1;
    buf[2] = h.protocol_major;
    buf[3] = h.msg_family;
    buf[4] = h.msg_type;
    buf[5] = h.queue_class;
    buf[6] = h.flags;
    buf[7] = h.frame_seq;
    wr16(buf + 8, payload_len);
    if (payload_len > 0 && payload != nullptr)
    {
        for (std::uint16_t i = 0; i < payload_len; ++i)
        {
            buf[10 + i] = payload[i];
        }
    }
    wr16(buf + 10 + payload_len, crc16(buf + 2, static_cast<std::uint16_t>(8 + payload_len)));
    return total;
}

CodecResult decode_operation_request(const std::uint8_t* p, std::uint16_t len, OperationRequest& out)
{
    constexpr std::uint16_t kFixed = 18; // 4+4+2+1+2+4+1
    if (len < kFixed)
    {
        return CodecResult::Truncated;
    }
    out.request_id = rd32(p);
    out.controller_epoch = rd32(p + 4);
    out.authority_id = rd16(p + 8);
    out.role = p[10];
    out.operation_type = rd16(p + 11);
    out.parent_operation_id = rd32(p + 13);
    out.params_len = p[17];
    if (out.params_len > MaxParams)
    {
        return CodecResult::OutOfBounds;
    }
    const std::uint16_t expected = static_cast<std::uint16_t>(kFixed + out.params_len);
    if (len != expected)
    {
        return CodecResult::OutOfBounds; // trailing bytes: malformed, rejected
    }
    for (std::uint8_t i = 0; i < out.params_len; ++i)
    {
        out.params[i] = p[kFixed + i];
    }
    return CodecResult::Ok;
}

CodecResult decode_stop_intent(const std::uint8_t* p, std::uint16_t len, StopIntent& out)
{
    if (len < 4)
    {
        return CodecResult::Truncated;
    }
    out.operation_id = rd32(p);
    return len == 4 ? CodecResult::Ok : CodecResult::OutOfBounds;
}

CodecResult decode_query(const std::uint8_t* p, std::uint16_t len, Query& out)
{
    if (len < 1)
    {
        return CodecResult::Truncated;
    }
    out.sections_mask = p[0];
    return len == 1 ? CodecResult::Ok : CodecResult::OutOfBounds;
}

CodecResult decode_subscribe(const std::uint8_t* p, std::uint16_t len, Subscribe& out)
{
    if (len < 6)
    {
        return CodecResult::Truncated;
    }
    out.class_mask = p[0];
    out.filter = p[1];
    out.min_interval_ms = rd16(p + 2);
    out.max_bytes_per_tick = rd16(p + 4);
    return len == 6 ? CodecResult::Ok : CodecResult::OutOfBounds;
}

CodecResult decode_unsubscribe(const std::uint8_t* p, std::uint16_t len, Unsubscribe& out)
{
    if (len < 1)
    {
        return CodecResult::Truncated;
    }
    out.sub_id = p[0];
    return len == 1 ? CodecResult::Ok : CodecResult::OutOfBounds;
}

std::uint16_t encode_ack_pos(std::uint8_t* buf, std::uint16_t cap, const AdmissionAckPositive& a)
{
    constexpr std::uint16_t kSize = 18; // 4+4+4+2+4
    if (cap < kSize)
    {
        return 0;
    }
    wr32(buf, a.request_id);
    wr32(buf + 4, a.controller_epoch);
    wr32(buf + 8, a.operation_id);
    wr16(buf + 12, a.operation_type);
    wr32(buf + 14, a.parent_operation_id);
    return kSize;
}

std::uint16_t encode_ack_neg(std::uint8_t* buf, std::uint16_t cap, const AdmissionAckNegative& a)
{
    constexpr std::uint16_t kSize = 9; // 4+4+1; NO operationId field (#47 section 18 #5)
    if (cap < kSize)
    {
        return 0;
    }
    wr32(buf, a.request_id);
    wr32(buf + 4, a.controller_epoch);
    buf[8] = a.reject_code;
    return kSize;
}

std::uint16_t encode_sub_ack(std::uint8_t* buf, std::uint16_t cap, const SubscriptionAck& a)
{
    constexpr std::uint16_t kSize = 3;
    if (cap < kSize)
    {
        return 0;
    }
    buf[0] = a.sub_id;
    buf[1] = a.accepted ? 1 : 0;
    buf[2] = a.reject_code;
    return kSize;
}

CodecResult decode_ack_pos(const std::uint8_t* p, std::uint16_t len, AdmissionAckPositive& out)
{
    constexpr std::uint16_t kSize = 18;
    if (len < kSize)
    {
        return CodecResult::Truncated;
    }
    out.request_id = rd32(p);
    out.controller_epoch = rd32(p + 4);
    out.operation_id = rd32(p + 8);
    out.operation_type = rd16(p + 12);
    out.parent_operation_id = rd32(p + 14);
    return len == kSize ? CodecResult::Ok : CodecResult::OutOfBounds;
}

CodecResult decode_ack_neg(const std::uint8_t* p, std::uint16_t len, AdmissionAckNegative& out)
{
    constexpr std::uint16_t kSize = 9;
    if (len < kSize)
    {
        return CodecResult::Truncated;
    }
    out.request_id = rd32(p);
    out.controller_epoch = rd32(p + 4);
    out.reject_code = p[8];
    return len == kSize ? CodecResult::Ok : CodecResult::OutOfBounds;
}

CodecResult decode_sub_ack(const std::uint8_t* p, std::uint16_t len, SubscriptionAck& out)
{
    constexpr std::uint16_t kSize = 3;
    if (len < kSize)
    {
        return CodecResult::Truncated;
    }
    out.sub_id = p[0];
    out.accepted = p[1] != 0;
    out.reject_code = p[2];
    return len == kSize ? CodecResult::Ok : CodecResult::OutOfBounds;
}

std::uint32_t request_fingerprint(const OperationRequest& r)
{
    // Canonical fields: role, operation_type (LE), parent_operation_id (LE),
    // params. CRC-32 (IEEE 802.3).
    std::uint8_t buf[1 + 2 + 4 + MaxParams];
    std::uint16_t n = 0;
    buf[n++] = r.role;
    buf[n++] = static_cast<std::uint8_t>(r.operation_type & 0xFFu);
    buf[n++] = static_cast<std::uint8_t>((r.operation_type >> 8) & 0xFFu);
    buf[n++] = static_cast<std::uint8_t>(r.parent_operation_id & 0xFFu);
    buf[n++] = static_cast<std::uint8_t>((r.parent_operation_id >> 8) & 0xFFu);
    buf[n++] = static_cast<std::uint8_t>((r.parent_operation_id >> 16) & 0xFFu);
    buf[n++] = static_cast<std::uint8_t>((r.parent_operation_id >> 24) & 0xFFu);
    const std::uint8_t plen = r.params_len <= MaxParams ? r.params_len : static_cast<std::uint8_t>(MaxParams);
    for (std::uint8_t i = 0; i < plen; ++i)
    {
        buf[n++] = r.params[i];
    }

    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint16_t i = 0; i < n; ++i)
    {
        crc ^= buf[i];
        for (std::uint8_t b = 0; b < 8; ++b)
        {
            crc = (crc & 1u) != 0u ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

} // namespace codec
} // namespace v3
