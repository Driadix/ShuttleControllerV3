// Observability record codec tests (design docs/observability-design-v3.md
// section 7.3 T16): envelope/telemetry/event/log/trace round-trips, fixed
// per-class layouts (10/14 B), bounds checks, LE, bit-field contract.
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "domain/codec.h"

namespace
{

using v3::codec::CodecResult;
using v3::codec::Envelope;
using v3::codec::EventBody;
using v3::codec::LogBody;
using v3::codec::TelemetryBody;
using v3::codec::TimeValidity;
using v3::codec::TraceBodyHeader;

TEST(ObservabilityCodec, EnvelopeTelemetryLayout)
{
    // telemetry: 10 B, no wall_time.
    Envelope e;
    e.class_id = static_cast<std::uint8_t>(v3::codec::QueueClass::Telemetry); // 3
    e.time_validity = TimeValidity::Unsynced;
    e.controller_epoch = 0x11223344u;
    e.monotonic_tick = 0x55667788u;
    e.seq = 0x5A;
    e.wall_time = 0xDEADBEEFu; // must NOT be written for telemetry

    std::uint8_t buf[Envelope::SizeWithWall];
    const std::uint16_t n = v3::codec::encode_envelope(buf, sizeof(buf), e, /*with_wall=*/false);
    ASSERT_EQ(n, Envelope::SizeNoWall); // 10

    Envelope out;
    ASSERT_EQ(v3::codec::decode_envelope(buf, n, out, /*with_wall=*/false), CodecResult::Ok);
    EXPECT_EQ(out.class_id, e.class_id);
    EXPECT_EQ(out.time_validity, TimeValidity::Unsynced);
    EXPECT_EQ(out.controller_epoch, e.controller_epoch);
    EXPECT_EQ(out.monotonic_tick, e.monotonic_tick);
    EXPECT_EQ(out.seq, e.seq);
    EXPECT_EQ(out.wall_time, 0u); // no wall in telemetry layout
    // Bit-field contract: class_id 3 bits, time_validity 2 bits, LSB-first.
    EXPECT_EQ(buf[0] & 0x07u, 3u);      // class_id
    EXPECT_EQ((buf[0] >> 3) & 0x03u, 0u); // time_validity
}

TEST(ObservabilityCodec, EnvelopeEventsLayoutWithWall)
{
    Envelope e;
    e.class_id = static_cast<std::uint8_t>(v3::codec::QueueClass::Events); // 4
    e.time_validity = TimeValidity::RtcOnly;
    e.controller_epoch = 1u;
    e.monotonic_tick = 2u;
    e.seq = 3u;
    e.wall_time = 0x01020304u;

    std::uint8_t buf[Envelope::SizeWithWall];
    const std::uint16_t n = v3::codec::encode_envelope(buf, sizeof(buf), e, /*with_wall=*/true);
    ASSERT_EQ(n, Envelope::SizeWithWall); // 14

    Envelope out;
    ASSERT_EQ(v3::codec::decode_envelope(buf, n, out, /*with_wall=*/true), CodecResult::Ok);
    EXPECT_EQ(out.class_id, 4u);
    EXPECT_EQ(out.time_validity, TimeValidity::RtcOnly);
    EXPECT_EQ(out.wall_time, 0x01020304u);
}

TEST(ObservabilityCodec, EnvelopeClassIdNeeds3Bits)
{
    // QueueClass::Events = 4 (0b100) must survive the 3-bit field (regression
    // for pass-1 finding F2: a 2-bit field would truncate to 0).
    for (std::uint8_t cls = 3; cls <= 6; ++cls)
    {
        Envelope e;
        e.class_id = cls;
        std::uint8_t buf[Envelope::SizeNoWall];
        const std::uint16_t n = v3::codec::encode_envelope(buf, sizeof(buf), e, false);
        ASSERT_EQ(n, Envelope::SizeNoWall);
        Envelope out;
        ASSERT_EQ(v3::codec::decode_envelope(buf, n, out, false), CodecResult::Ok);
        EXPECT_EQ(out.class_id, cls);
    }
}

TEST(ObservabilityCodec, EnvelopeTruncated)
{
    Envelope out;
    EXPECT_EQ(v3::codec::decode_envelope(nullptr, 0, out, false), CodecResult::Truncated);
    std::uint8_t buf[9] = {};
    EXPECT_EQ(v3::codec::decode_envelope(buf, 9, out, false), CodecResult::Truncated);
    EXPECT_EQ(v3::codec::decode_envelope(buf, 9, out, true), CodecResult::Truncated);
}

TEST(ObservabilityCodec, TelemetryBodyRoundTrip)
{
    TelemetryBody b;
    b.op_state = 7;
    b.position_mm = 123456u;
    b.speed_mm_s = 987u;
    b.health = 2;
    b.fault_mask = 0x0102;
    b.warning_mask = 0x0804;
    b.battery_charge = 90;
    b.battery_voltage_mv = 25500u;
    b.pallet_count = 3;
    b.state_flags = 0x03;

    std::uint8_t buf[32];
    const std::uint16_t n = v3::codec::encode_telemetry_body(buf, sizeof(buf), b);
    ASSERT_EQ(n, 18u); // packed size (F11 fix)

    TelemetryBody out;
    ASSERT_EQ(v3::codec::decode_telemetry_body(buf, n, out), CodecResult::Ok);
    EXPECT_EQ(out.op_state, b.op_state);
    EXPECT_EQ(out.position_mm, b.position_mm);
    EXPECT_EQ(out.speed_mm_s, b.speed_mm_s);
    EXPECT_EQ(out.health, b.health);
    EXPECT_EQ(out.fault_mask, b.fault_mask);
    EXPECT_EQ(out.warning_mask, b.warning_mask);
    EXPECT_EQ(out.battery_charge, b.battery_charge);
    EXPECT_EQ(out.battery_voltage_mv, b.battery_voltage_mv);
    EXPECT_EQ(out.pallet_count, b.pallet_count);
    EXPECT_EQ(out.state_flags, b.state_flags);
}

TEST(ObservabilityCodec, TelemetryBodyTruncated)
{
    TelemetryBody out;
    std::uint8_t buf[17] = {};
    EXPECT_EQ(v3::codec::decode_telemetry_body(buf, 17, out), CodecResult::Truncated);
    EXPECT_EQ(v3::codec::decode_telemetry_body(buf, 18, out), CodecResult::Ok);
}

TEST(ObservabilityCodec, EventBodyRoundTrip)
{
    EventBody b;
    b.event_id = 0x0502;
    b.severity = 1;
    b.ctx_kind = 3;
    b.ctx_value = 0xAABBCCDDu;
    b.ctx_value2 = 0x11223344u;

    std::uint8_t buf[32];
    const std::uint16_t n = v3::codec::encode_event_body(buf, sizeof(buf), b);
    ASSERT_EQ(n, 12u);
    EventBody out;
    ASSERT_EQ(v3::codec::decode_event_body(buf, n, out), CodecResult::Ok);
    EXPECT_EQ(out.event_id, b.event_id);
    EXPECT_EQ(out.severity, b.severity);
    EXPECT_EQ(out.ctx_kind, b.ctx_kind);
    EXPECT_EQ(out.ctx_value, b.ctx_value);
    EXPECT_EQ(out.ctx_value2, b.ctx_value2);
}

TEST(ObservabilityCodec, LogBodyTruncation)
{
    LogBody b;
    b.level = 2;
    b.module_id = 5;
    b.text_len = 80;
    for (std::uint16_t i = 0; i < 80; ++i)
    {
        b.text[i] = static_cast<std::uint8_t>('a' + (i % 26));
    }

    std::uint8_t buf[128];
    const std::uint16_t n = v3::codec::encode_log_body(buf, sizeof(buf), b);
    ASSERT_EQ(n, 82u); // 2 + 80

    LogBody out;
    ASSERT_EQ(v3::codec::decode_log_body(buf, n, out), CodecResult::Ok);
    EXPECT_EQ(out.level, 2u);
    EXPECT_EQ(out.module_id, 5u);
    EXPECT_EQ(out.text_len, 80u);
    EXPECT_EQ(std::memcmp(out.text, b.text, 80), 0);

    // Text longer than 80 is truncated WITHOUT chunk split (MAJOR-2 fix).
    LogBody long_b;
    long_b.text_len = 200;
    std::uint8_t small[32];
    const std::uint16_t sn = v3::codec::encode_log_body(small, sizeof(small), long_b);
    // encode writes only the bounded portion; caller truncates len at 80 first.
    EXPECT_LE(sn, 82u);
}

TEST(ObservabilityCodec, TraceHeaderRoundTrip)
{
    TraceBodyHeader h;
    h.kind = 0;
    h.trigger_event_id = 0x0801;
    h.trigger_tick = 0xCAFEBABEu;
    h.payload_len = 489u;

    std::uint8_t buf[32];
    const std::uint16_t n = v3::codec::encode_trace_header(buf, sizeof(buf), h);
    ASSERT_EQ(n, 11u);
    TraceBodyHeader out;
    ASSERT_EQ(v3::codec::decode_trace_header(buf, n, out), CodecResult::Ok);
    EXPECT_EQ(out.kind, h.kind);
    EXPECT_EQ(out.trigger_event_id, h.trigger_event_id);
    EXPECT_EQ(out.trigger_tick, h.trigger_tick);
    EXPECT_EQ(out.payload_len, h.payload_len);
}

TEST(ObservabilityCodec, MaxRecordFitsMtu)
{
    // Worst-case single record: envelope(14) + LogBody(82) = 96 B payload;
    // canonical frame adds sync 2 + header 8 + crc 2 = 12 => 108 B <= MTU 128.
    constexpr std::uint16_t kMaxPayload = Envelope::SizeWithWall + 82;
    EXPECT_LE(kMaxPayload, v3::codec::MaxPayload);
    EXPECT_LE(kMaxPayload + 12, v3::codec::Mtu);
}

} // namespace
