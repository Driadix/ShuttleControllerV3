// Contract core tests (design docs/operation-runtime-design-v3.md section 7.3
// T1-T9): frame round-trip, checksum, truncation, bounds, version matrix,
// typed codecs, negative-ACK shape, encode capacity, fuzz property.
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "domain/codec.h"

namespace
{

using v3::codec::AdmissionAckNegative;
using v3::codec::AdmissionAckPositive;
using v3::codec::CodecResult;
using v3::codec::DecodeResult;
using v3::codec::Header;
using v3::codec::OperationRequest;
using v3::codec::TransportError;

// Deterministic PRNG (xorshift32) for the fuzz property test (no external dep).
std::uint32_t g_rng = 0x12345678u;
std::uint32_t next_u32()
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

Header control_header(std::uint8_t msg_type, std::uint16_t payload_len)
{
    Header h;
    h.msg_family = static_cast<std::uint8_t>(v3::codec::Family::Control);
    h.msg_type = msg_type;
    h.queue_class = static_cast<std::uint8_t>(v3::codec::QueueClass::Control);
    h.payload_len = payload_len;
    return h;
}

TEST(Codec, FrameRoundTrip)
{
    std::uint8_t payload[v3::codec::MaxPayload];
    for (std::uint16_t i = 0; i < sizeof(payload); ++i)
    {
        payload[i] = static_cast<std::uint8_t>(i * 3);
    }
    std::uint8_t frame[v3::codec::Mtu];
    Header h = control_header(static_cast<std::uint8_t>(v3::codec::MsgControl::OperationRequest), 40);
    h.frame_seq = 0x5A;
    const std::uint16_t flen = v3::codec::encode(frame, sizeof(frame), h, payload, 40);
    EXPECT_EQ(flen, 52u); // 12 + 40

    const DecodeResult dr = v3::codec::decode(frame, flen);
    ASSERT_TRUE(dr.ok());
    EXPECT_EQ(dr.frame.header.msg_family, h.msg_family);
    EXPECT_EQ(dr.frame.header.msg_type, h.msg_type);
    EXPECT_EQ(dr.frame.header.queue_class, h.queue_class);
    EXPECT_EQ(dr.frame.header.frame_seq, h.frame_seq);
    EXPECT_EQ(dr.frame.header.payload_len, 40u);
    EXPECT_EQ(std::memcmp(dr.frame.payload, payload, 40), 0);
}

TEST(Codec, CrcDetectsSingleBitFlip)
{
    std::uint8_t payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    std::uint8_t frame[v3::codec::Mtu];
    const Header h = control_header(1, 10);
    const std::uint16_t flen = v3::codec::encode(frame, sizeof(frame), h, payload, 10);
    frame[12] ^= 0x01; // flip one payload bit
    const DecodeResult dr = v3::codec::decode(frame, flen);
    EXPECT_FALSE(dr.ok());
    EXPECT_EQ(dr.error, TransportError::BadCrc);
}

TEST(Codec, TruncatedFrameRejected)
{
    std::uint8_t buf[11] = {};
    EXPECT_EQ(v3::codec::decode(buf, 11).error, TransportError::Truncated); // < 12 min

    std::uint8_t frame[v3::codec::Mtu];
    const Header h = control_header(1, 10);
    const std::uint16_t flen = v3::codec::encode(frame, sizeof(frame), h, nullptr, 0);
    (void)flen;
    // Craft header claiming 10 payload bytes but supply only 15 total (need 22).
    std::uint8_t short_buf[15] = {};
    short_buf[0] = v3::codec::Sync0;
    short_buf[1] = v3::codec::Sync1;
    short_buf[2] = v3::codec::ProtocolMajor;
    short_buf[3] = static_cast<std::uint8_t>(v3::codec::Family::Control);
    short_buf[8] = 10; // payload_len (LE) = 10
    short_buf[9] = 0;
    EXPECT_EQ(v3::codec::decode(short_buf, 15).error, TransportError::Truncated);
}

TEST(Codec, PayloadLengthBound)
{
    std::uint8_t buf[32] = {};
    buf[0] = v3::codec::Sync0;
    buf[1] = v3::codec::Sync1;
    buf[2] = v3::codec::ProtocolMajor;
    buf[3] = static_cast<std::uint8_t>(v3::codec::Family::Control);
    buf[8] = 120; // payload_len > MaxPayload (116)
    buf[9] = 0;
    EXPECT_EQ(v3::codec::decode(buf, sizeof(buf)).error, TransportError::PayloadTooLong);

    std::uint8_t frame[v3::codec::Mtu];
    std::uint8_t payload[v3::codec::MaxPayload + 8] = {};
    EXPECT_EQ(v3::codec::encode(frame, sizeof(frame), control_header(1, 0), payload,
                                static_cast<std::uint16_t>(v3::codec::MaxPayload + 8)),
              0u);
}

TEST(Codec, VersionAndFamilyMatrix)
{
    std::uint8_t payload[4] = {0, 0, 0, 0};
    std::uint8_t frame[v3::codec::Mtu];
    Header h = control_header(1, 4);
    h.protocol_major = 2; // major mismatch: hard fail (#47 section 5.1)
    const std::uint16_t flen = v3::codec::encode(frame, sizeof(frame), h, payload, 4);
    EXPECT_EQ(v3::codec::decode(frame, flen).error, TransportError::UnsupportedMajor);

    h = control_header(1, 4);
    h.msg_family = 0; // unknown family
    const std::uint16_t flen2 = v3::codec::encode(frame, sizeof(frame), h, payload, 4);
    EXPECT_EQ(v3::codec::decode(frame, flen2).error, TransportError::UnknownFamily);
}

TEST(Codec, OperationRequestCodecBounds)
{
    OperationRequest req;
    req.request_id = 0xDEADBEEFu;
    req.controller_epoch = 7;
    req.authority_id = 5;
    req.role = 1;
    req.operation_type = 100;
    req.parent_operation_id = 0x11223344u;
    req.params_len = 6;
    for (std::uint8_t i = 0; i < 6; ++i)
    {
        req.params[i] = static_cast<std::uint8_t>(0xA0 + i);
    }
    std::uint8_t payload[v3::codec::MaxPayload];
    std::uint16_t plen = 0;
    {
        // manual LE serialize (mirrors the codec layout)
        plen = 24;
        payload[0] = 0xEF; payload[1] = 0xBE; payload[2] = 0xAD; payload[3] = 0xDE;
        payload[4] = 7; payload[5] = 0; payload[6] = 0; payload[7] = 0;
        payload[8] = 5; payload[9] = 0;
        payload[10] = 1;
        payload[11] = 100; payload[12] = 0;
        payload[13] = 0x44; payload[14] = 0x33; payload[15] = 0x22; payload[16] = 0x11;
        payload[17] = 6;
        for (std::uint8_t i = 0; i < 6; ++i)
        {
            payload[18 + i] = static_cast<std::uint8_t>(0xA0 + i);
        }
    }
    OperationRequest out;
    EXPECT_EQ(v3::codec::decode_operation_request(payload, plen, out), CodecResult::Ok);
    EXPECT_EQ(out.request_id, req.request_id);
    EXPECT_EQ(out.controller_epoch, 7u);
    EXPECT_EQ(out.authority_id, 5u);
    EXPECT_EQ(out.role, 1u);
    EXPECT_EQ(out.operation_type, 100u);
    EXPECT_EQ(out.parent_operation_id, 0x11223344u);
    EXPECT_EQ(out.params_len, 6u);
    EXPECT_EQ(std::memcmp(out.params, req.params, 6), 0);

    EXPECT_EQ(v3::codec::decode_operation_request(payload, 17, out), CodecResult::Truncated);
    payload[17] = 65; // params_len > MaxParams
    EXPECT_EQ(v3::codec::decode_operation_request(payload, plen, out), CodecResult::OutOfBounds);
    payload[17] = 6;
    payload[24] = 0xFF; // trailing byte: malformed
    EXPECT_EQ(v3::codec::decode_operation_request(payload, 25, out), CodecResult::OutOfBounds);
}

TEST(Codec, NegativeAckNeverCarriesOperationId)
{
    // #47 section 18 #5: negative ACK has no operationId field - the struct
    // itself is 9 bytes (requestId + epoch + code).
    EXPECT_EQ(sizeof(AdmissionAckNegative), 9u);
    AdmissionAckNegative a;
    a.request_id = 42;
    a.controller_epoch = 7;
    a.reject_code = static_cast<std::uint8_t>(v3::codec::RejectCode::HealthGate);
    std::uint8_t buf[v3::codec::MaxPayload];
    const std::uint16_t plen = v3::codec::encode_ack_neg(buf, sizeof(buf), a);
    EXPECT_EQ(plen, 9u);
    AdmissionAckNegative out;
    EXPECT_EQ(v3::codec::decode_ack_neg(buf, plen, out), CodecResult::Ok);
    EXPECT_EQ(out.request_id, 42u);
    EXPECT_EQ(out.reject_code, static_cast<std::uint8_t>(v3::codec::RejectCode::HealthGate));
}

TEST(Codec, AckCodecsRoundTrip)
{
    AdmissionAckPositive a;
    a.request_id = 1;
    a.controller_epoch = 7;
    a.operation_id = 0xABCDEF01u;
    a.operation_type = 200;
    a.parent_operation_id = 0x10203040u;
    std::uint8_t buf[v3::codec::MaxPayload];
    const std::uint16_t plen = v3::codec::encode_ack_pos(buf, sizeof(buf), a);
    EXPECT_EQ(plen, 18u);
    AdmissionAckPositive out;
    EXPECT_EQ(v3::codec::decode_ack_pos(buf, plen, out), CodecResult::Ok);
    EXPECT_EQ(out.operation_id, a.operation_id);
    EXPECT_EQ(out.operation_type, 200u);
    EXPECT_EQ(out.parent_operation_id, a.parent_operation_id);

    EXPECT_EQ(v3::codec::encode_ack_pos(buf, 17, a), 0u); // capacity error
    EXPECT_EQ(v3::codec::encode_ack_neg(buf, 8, AdmissionAckNegative{}), 0u);
}

TEST(Codec, FuzzNeverAcceptsInvalidOrCrashes)
{
    // Property: for arbitrary bytes, decode either fails OR the frame is
    // CRC-valid (nothing accepted without integrity); a valid round-trip
    // decodes back to the same header/payload.
    std::uint8_t buf[v3::codec::Mtu];
    for (std::uint32_t iter = 0; iter < 500; ++iter)
    {
        const std::uint16_t len = static_cast<std::uint16_t>(next_u32() % (v3::codec::Mtu + 1));
        for (std::uint16_t i = 0; i < len; ++i)
        {
            buf[i] = static_cast<std::uint8_t>(next_u32());
        }
        const DecodeResult dr = v3::codec::decode(buf, len);
        if (dr.ok())
        {
            // Accepted only with a valid checksum over the claimed header+payload.
            const std::uint16_t check_len = static_cast<std::uint16_t>(8 + dr.frame.payload_len);
            const std::uint16_t stored = static_cast<std::uint16_t>(buf[10 + dr.frame.payload_len] |
                                                                    (static_cast<std::uint16_t>(buf[11 + dr.frame.payload_len]) << 8));
            EXPECT_EQ(stored, v3::codec::crc16(buf + 2, check_len));
            // Round-trip: re-encode the same header+payload, decode again.
            std::uint8_t re[v3::codec::Mtu];
            const std::uint16_t rlen = v3::codec::encode(re, sizeof(re), dr.frame.header,
                                                         dr.frame.payload, dr.frame.payload_len);
            ASSERT_GT(rlen, 0u);
            const DecodeResult dr2 = v3::codec::decode(re, rlen);
            ASSERT_TRUE(dr2.ok());
            EXPECT_EQ(dr2.frame.header.msg_family, dr.frame.header.msg_family);
            EXPECT_EQ(dr2.frame.payload_len, dr.frame.payload_len);
        }
    }
}

} // namespace
