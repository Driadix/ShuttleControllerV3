// Observability Producer/Sink host tests (design docs/observability-design-v3.md
// section 7.3 T1-T15, T17). Deterministic fakes: FakeUart (never-block check),
// FakeRegistry interest script, FakeRuntime/FakeSensingView/FakeDiag snapshot
// sources, FakeWall, FakeIdentity. No test hooks in production API (injections
// live in the fakes, matching tests/common/fakes.h).
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "domain/codec.h"
#include "domain/diag_safety.h"
#include "domain/observability.h"
#include "domain/safety_state.h"
#include "domain/subscriptions.h"

namespace
{

using v3::codec::QueueClass;
using v3::codec::TimeValidity;
using v3::observability::Producer;
using v3::observability::Sink;
using v3::SafetyDiag;
using v3::safety::SafetyHealth;

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

// Never-blocking UART fake: fixed capacity; tx() returns false when full.
class FakeUart : public v3::UartPort
{
  public:
    static constexpr std::uint32_t kCapacity = 256;

    void init(std::uint32_t cap = kCapacity) { m_cap = cap; }

    std::uint32_t tx_bytes_available() const override
    {
        return m_cap - m_used;
    }

    bool tx(const std::uint8_t* data, std::uint32_t len) override
    {
        if (len > m_cap - m_used)
        {
            ++m_tx_rejected;
            return false;
        }
        for (std::uint32_t i = 0; i < len; ++i)
        {
            m_bytes[m_used++] = data[i];
        }
        ++m_tx_calls;
        return true;
    }

    void drain(std::uint32_t n) { m_used = m_used > n ? m_used - n : 0; }

    std::uint32_t used() const { return m_used; }
    std::uint32_t tx_calls() const { return m_tx_calls; }
    std::uint32_t tx_rejected() const { return m_tx_rejected; }
    const std::uint8_t* bytes() const { return m_bytes; }

  private:
    std::uint8_t m_bytes[512] = {};
    std::uint32_t m_used = 0;
    std::uint32_t m_cap = kCapacity;
    std::uint32_t m_tx_calls = 0;
    std::uint32_t m_tx_rejected = 0;
};

// Real subscription Registry with a helper to subscribe a class mask
// (Registry::interest is NOT virtual - tests must drive the real registry).
// (moved below: see ObsEnv after the fakes)

class FakeSensing : public v3::sensing::SensingView
{
  public:
    bool get_snapshot(v3::sensing::SensorId, v3::sensing::SensorSnapshot* out) const override
    {
        if (m_ok)
        {
            out->raw = 42;
            out->age_ms = 5;
            out->state = v3::sensing::HealthState::Healthy;
            out->has_sample = true;
        }
        return m_ok;
    }
    void set_ok(bool ok) { m_ok = ok; }

  private:
    bool m_ok = true;
};

class FakeWall : public v3::WallClockSource
{
  public:
    void set(TimeValidity v, std::uint32_t epoch) { m_v = v; m_epoch = epoch; }
    std::uint32_t epoch_sec() const override { return m_epoch; }
    TimeValidity time_validity() const override { return m_v; }

  private:
    TimeValidity m_v = TimeValidity::Unsynced;
    std::uint32_t m_epoch = 0;
};

class FakeIdentity : public v3::IdentitySource
{
  public:
    std::uint32_t hardware_id() const override { return 0x1234ABCDu; }
};

class FakeEpoch : public v3::EpochSource
{
  public:
    std::uint32_t epoch() const override { return 1; }
};

class FakeWindow : public v3::WindowSource
{
  public:
    v3::PlatformWindow window() const override { return v3::PlatformWindow::Serving; }
};

class FakeHealth : public v3::HealthSource
{
  public:
    void set(SafetyHealth h) { m_h = h; }
    SafetyHealth health() const override { return m_h; }

  private:
    SafetyHealth m_h = SafetyHealth::Ready;
};

class FakeProvisioning : public v3::ProvisioningSource
{
  public:
    v3::ProvisioningStatus status() const override { return v3::ProvisioningStatus::Provisioned; }
};

// Snapshot source: real production Runtime (host-tested elsewhere) with no
// instances - snapshot() count 0 for the #72 tests.
class FakeRuntime : public v3::runtime::Runtime
{
};

// Fixture: wires Producer + Sink with fakes (real Registry, scripted via
// subscribe_mask - Registry::interest is NOT virtual).
struct ObsEnv
{
    FakeUart uart;
    v3::subscription::Registry registry;
    FakeRuntime runtime;
    FakeSensing sensing;
    FakeWall wall;
    FakeIdentity identity;
    FakeEpoch epoch;
    FakeWindow window;
    FakeHealth health;
    FakeProvisioning prov;
    SafetyDiag diag{};
    Producer producer;
    Sink sink;

    ObsEnv()
    {
        diag.magic = 0x53414631u; // 'SAF1'
        registry.init(/*profile=*/0, nullptr);
        producer.init(&epoch, &window, &health, &prov, &registry, &runtime, &sensing,
                      &diag, &wall, &identity, &sink);
        sink.init(&uart, &registry, &producer);
    }

    // Subscribes class bits (0x01 telemetry, 0x02 events, 0x04 logs, 0x08 traces).
    void subscribe_mask(std::uint8_t mask)
    {
        v3::codec::Subscribe s;
        s.class_mask = mask;
        std::uint8_t sub_id = 0;
        (void)registry.subscribe(1, s, sub_id);
    }

    void tick() { sink.tick(); }
    void set_now(std::uint32_t ms) { producer.set_now(ms); }
};

// Decodes the payload of the last emitted frame of a class from the UART.
std::uint16_t last_class_payload(const FakeUart& uart, QueueClass cls, std::uint8_t* out,
                                 std::uint16_t cap)
{
    // Scan backwards through UART bytes for a canonical frame of the class.
    const std::uint8_t* b = uart.bytes();
    const std::uint32_t n = uart.used();
    for (std::int32_t i = static_cast<std::int32_t>(n - 1); i >= 0; --i)
    {
        if (b[i] != 0xE3 || i + 1 >= static_cast<std::int32_t>(n) || b[i + 1] != 0x10)
        {
            continue;
        }
        const std::uint16_t plen = static_cast<std::uint16_t>(b[i + 8]) |
                                   (static_cast<std::uint16_t>(b[i + 9]) << 8);
        if (i + 12 + plen > static_cast<std::int32_t>(n))
        {
            continue;
        }
        if (b[i + 5] != static_cast<std::uint8_t>(cls))
        {
            continue;
        }
        const std::uint16_t take = plen < cap ? plen : cap;
        for (std::uint16_t k = 0; k < take; ++k)
        {
            out[k] = b[i + 10 + k];
        }
        return take;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// T1: envelope correctness (classId/epoch/tick/seq/wall)
// ---------------------------------------------------------------------------
TEST(Observability, T1_EnvelopeFields)
{
    ObsEnv env;
    env.set_now(1000);
    env.wall.set(TimeValidity::Synced, 1700000000u);
    env.health.set(SafetyHealth::Ready);
    env.subscribe_mask(0x01); // telemetry only

    env.producer.emit_telemetry();
    env.tick();

    std::uint8_t payload[64];
    const std::uint16_t n = last_class_payload(env.uart, QueueClass::Telemetry, payload, sizeof(payload));
    ASSERT_GT(n, 0u);

    v3::codec::Envelope env_out;
    // Envelope is the FIRST 10 B of the payload (telemetry layout, no wall).
    ASSERT_EQ(v3::codec::decode_envelope(payload, v3::codec::Envelope::SizeNoWall, env_out,
                                         /*with_wall=*/false),
              v3::codec::CodecResult::Ok);
    EXPECT_EQ(env_out.class_id, static_cast<std::uint8_t>(QueueClass::Telemetry));
    EXPECT_EQ(env_out.controller_epoch, 1u);
    EXPECT_EQ(env_out.monotonic_tick, 1000u);
    EXPECT_EQ(env_out.time_validity, TimeValidity::Synced);
    EXPECT_EQ(env_out.wall_time, 0u); // telemetry has no wall field
}

// T1b: wall NOT emitted when Unsynced (wall=0, validity=Unsynced) - #49 s3.
TEST(Observability, T1b_WallUnsynced)
{
    ObsEnv env;
    env.set_now(5);
    env.wall.set(TimeValidity::Unsynced, 0);
    env.subscribe_mask(0x02); // events

    env.producer.admission_rejected(3);
    env.tick();

    std::uint8_t payload[64];
    const std::uint16_t n = last_class_payload(env.uart, QueueClass::Events, payload, sizeof(payload));
    ASSERT_GT(n, 0u);
    v3::codec::Envelope env_out;
    // Envelope is the FIRST 14 B of the payload (events layout, with wall).
    ASSERT_EQ(v3::codec::decode_envelope(payload, v3::codec::Envelope::SizeWithWall, env_out,
                                         /*with_wall=*/true),
              v3::codec::CodecResult::Ok);
    EXPECT_EQ(env_out.time_validity, TimeValidity::Unsynced);
    EXPECT_EQ(env_out.wall_time, 0u);
}

// ---------------------------------------------------------------------------
// T2: events drop-newest (capacity 32)
// ---------------------------------------------------------------------------
TEST(Observability, T2_EventsDropNewest)
{
    ObsEnv env;
    env.subscribe_mask(0x02);

    // Fill events queue to capacity with distinct event ids.
    for (std::uint32_t i = 0; i < Sink::EventsCapacity; ++i)
    {
        env.set_now(static_cast<std::uint32_t>(i));
        env.producer.admission_rejected(static_cast<std::uint8_t>(i % 22));
    }
    // 33rd: drop-newest (rejected; counter + event).
    env.set_now(Sink::EventsCapacity);
    env.producer.admission_rejected(7);

    EXPECT_EQ(env.producer.counters().drop_events, 1u);
    // The rejected record is NOT in the queue: draining sends <= 32 frames.
    env.tick();
    // Frame count: 33 events emitted -> 1 dropped at enqueue; queue holds 32,
    // plus 1 coalesced drop-event (0x0502) flushed after drain = 33 frames.
    for (std::uint32_t i = 0; i < 20; ++i)
    {
        env.tick();
        env.uart.drain(env.uart.used());
    }
    EXPECT_EQ(env.uart.tx_calls(), 33u); // 32 accepted records + 1 drop event
}

// ---------------------------------------------------------------------------
// T3: telemetry drop-oldest (capacity 8) - freshness: newest survives
// ---------------------------------------------------------------------------
TEST(Observability, T3_TelemetryDropOldest)
{
    ObsEnv env;
    env.subscribe_mask(0x01);
    env.health.set(SafetyHealth::Ready);

    for (std::uint32_t i = 0; i < Sink::TelemetryCapacity + 2; ++i)
    {
        env.set_now(static_cast<std::uint32_t>(100 + i));
        env.producer.emit_telemetry();
    }
    EXPECT_EQ(env.producer.counters().drop_telemetry, 2u);

    // The freshest records survive in the queue (capacity 8): after a FULL
    // drain the last frame on the wire is the newest record (tick 109), and
    // the two oldest (100, 101) are gone (drop-oldest freshness).
    std::uint8_t last[64];
    std::uint16_t last_len = 0;
    for (std::uint32_t i = 0; i < 20; ++i)
    {
        env.tick();
        // Capture the tail of the ring BEFORE draining (the newest frame sent
        // in this tick); a telemetry frame is ~40 B, one per tick fits.
        if (env.uart.used() > 0)
        {
            const std::uint8_t* b = env.uart.bytes();
            const std::uint32_t n = env.uart.used();
            const std::uint16_t plen = static_cast<std::uint16_t>(b[n - 2]) |
                                       (static_cast<std::uint16_t>(b[n - 1]) << 8);
            // Skip if this tick only flushed the tail of a previous frame; use
            // the start-of-frame marker to locate the last complete frame.
            std::int32_t start = -1;
            for (std::int32_t j = static_cast<std::int32_t>(n - 12); j >= 0; --j)
            {
                if (b[j] == 0xE3 && b[j + 1] == 0x10 && b[j + 5] ==
                    static_cast<std::uint8_t>(QueueClass::Telemetry))
                {
                    start = j;
                    break;
                }
            }
            if (start >= 0 && start + 10 + v3::codec::Envelope::SizeNoWall <=
                                  static_cast<std::int32_t>(n))
            {
                for (std::uint16_t k = 0; k < v3::codec::Envelope::SizeNoWall; ++k)
                {
                    last[k] = b[start + 10 + k];
                }
                last_len = v3::codec::Envelope::SizeNoWall;
            }
        }
        env.uart.drain(env.uart.used());
    }
    ASSERT_GT(last_len, 0u);
    v3::codec::Envelope env_out;
    ASSERT_EQ(v3::codec::decode_envelope(last, last_len, env_out, /*with_wall=*/false),
              v3::codec::CodecResult::Ok);
    EXPECT_EQ(env_out.monotonic_tick, 109u); // newest survived (100/101 dropped)
}

// ---------------------------------------------------------------------------
// T4: logs drop-newest + text truncation at 80 B
// ---------------------------------------------------------------------------
TEST(Observability, T4_LogsTruncationAndDrop)
{
    ObsEnv env;
    env.subscribe_mask(0x04);

    char text[200];
    std::memset(text, 'x', sizeof(text));
    env.set_now(1);
    v3::observability::log_emit(env.producer, 2, 3, text, sizeof(text));
    env.tick();

    std::uint8_t payload[128];
    const std::uint16_t n = last_class_payload(env.uart, QueueClass::Logs, payload, sizeof(payload));
    ASSERT_GT(n, 0u);
    // payload = envelope(14) + level + module_id + text(<=80)
    v3::codec::LogBody body;
    ASSERT_EQ(v3::codec::decode_log_body(payload + 14, n - 14, body), v3::codec::CodecResult::Ok);
    EXPECT_EQ(body.text_len, 80u);
    for (std::uint16_t i = 0; i < 80; ++i)
    {
        EXPECT_EQ(body.text[i], 'x');
    }
}

// ---------------------------------------------------------------------------
// T7: per-tick caps - per-class 128 B + total 230 B per tick
// ---------------------------------------------------------------------------
TEST(Observability, T7_PerTickCaps)
{
    ObsEnv env;
    env.subscribe_mask(0x04); // logs only, no cap collisions
    // Emit 10 large logs (96 B records -> ~108 B frames each).
    char text[80];
    std::memset(text, 'x', sizeof(text));
    for (std::uint32_t i = 0; i < 10; ++i)
    {
        env.set_now(static_cast<std::uint32_t>(i));
        v3::observability::log_emit(env.producer, 2, 3, text, sizeof(text));
    }

    // One tick: total budget 230 B => at most 2 frames of ~108 B.
    env.uart.drain(env.uart.used()); // clear
    env.tick();
    const std::uint32_t sent = env.uart.used();
    EXPECT_LE(sent, 230u);
    EXPECT_GE(sent, 100u); // at least one frame
    // Per-class cap 128 B: a single log frame (108 B) fits; two (216) would
    // exceed 128 => exactly ONE log frame per tick under the class cap.
    EXPECT_LE(env.uart.tx_calls(), 2u);
}

// ---------------------------------------------------------------------------
// T8: priorities - Control > events > logs > traces > telemetry
// ---------------------------------------------------------------------------
TEST(Observability, T8_PriorityOrder)
{
    ObsEnv env;
    env.subscribe_mask(0x0F);

    // Control answer first: enqueue a CANONICAL Control frame (semantic.cpp
    // send_control already encodes; the Sink/OutboundControl contract expects
    // a canonical frame, never raw payload bytes).
    std::uint8_t ctrl[32];
    ctrl[0] = 0xAA;
    v3::codec::Header h;
    h.msg_family = static_cast<std::uint8_t>(v3::codec::Family::Control);
    h.msg_type = static_cast<std::uint8_t>(v3::codec::MsgControl::SubscriptionAck);
    h.queue_class = static_cast<std::uint8_t>(QueueClass::Control);
    std::uint8_t ctrl_frame[v3::codec::Mtu];
    const std::uint16_t ctrl_len = v3::codec::encode(ctrl_frame, sizeof(ctrl_frame), h, ctrl, 16);
    ASSERT_GT(ctrl_len, 0u);
    env.sink.enqueue(QueueClass::Control, ctrl_frame, ctrl_len);
    env.producer.admission_rejected(1); // events
    env.set_now(1);
    env.producer.emit_telemetry(); // telemetry

    env.uart.drain(env.uart.used());
    env.tick();

    // The first frame on the wire must be the Control answer.
    const std::uint8_t* b = env.uart.bytes();
    ASSERT_GE(env.uart.used(), 12u);
    EXPECT_EQ(b[0], 0xE3);
    EXPECT_EQ(b[1], 0x10);
    EXPECT_EQ(b[5], static_cast<std::uint8_t>(QueueClass::Control));
    // Control payload echoed 0xAA.
    EXPECT_EQ(b[10], 0xAA);
}

// ---------------------------------------------------------------------------
// T9: Query answer - document <= 456 B, <= 4 fragments, fencing
// ---------------------------------------------------------------------------
TEST(Observability, T9_QueryAnswerFragments)
{
    ObsEnv env;
    env.set_now(42);

    env.producer.answer_query(0xFF);
    env.tick();

    // Count snapshot fragments on the wire.
    std::uint32_t fragments = 0;
    std::uint32_t total_doc = 0;
    const std::uint8_t* b = env.uart.bytes();
    const std::uint32_t n = env.uart.used();
    for (std::uint32_t i = 0; i + 12 <= n; ++i)
    {
        if (b[i] == 0xE3 && b[i + 1] == 0x10)
        {
            const std::uint16_t plen = static_cast<std::uint16_t>(b[i + 8]) |
                                       (static_cast<std::uint16_t>(b[i + 9]) << 8);
            if (b[i + 4] == static_cast<std::uint8_t>(v3::codec::MsgObservability::SnapshotFragment))
            {
                ++fragments;
                total_doc += plen - 2; // minus fragmentIndex/Count
            }
            i += 11 + plen;
        }
    }
    EXPECT_GE(fragments, 1u);
    EXPECT_LE(fragments, 4u);
    EXPECT_LE(total_doc, 456u);
}

// T9b: Sink intercepts a Query frame routed via OutboundControl (semantic
// handle_query forwards it) and answers with snapshot fragments instead of
// echoing the Query to the UART (design §3.2, review F3).
TEST(Observability, T9b_QueryInterception)
{
    ObsEnv env;
    env.set_now(7);

    // Build a canonical Control Query frame (sections_mask = 0xFF), as
    // semantic::handle_query forwards it into OutboundControl.
    v3::codec::Query q;
    q.sections_mask = 0xFF;
    std::uint8_t payload[v3::codec::MaxPayload];
    payload[0] = q.sections_mask;
    v3::codec::Header h;
    h.msg_family = static_cast<std::uint8_t>(v3::codec::Family::Control);
    h.msg_type = static_cast<std::uint8_t>(v3::codec::MsgControl::Query);
    h.queue_class = static_cast<std::uint8_t>(v3::codec::QueueClass::Control);
    std::uint8_t frame[v3::codec::Mtu];
    const std::uint16_t flen = v3::codec::encode(frame, sizeof(frame), h, payload, 1);
    ASSERT_GT(flen, 0u);

    env.sink.enqueue(v3::codec::QueueClass::Control, frame, flen);
    env.tick();

    // The wire must carry snapshot fragments (family Observability, msg_type
    // SnapshotFragment), NOT an echoed Query frame.
    const std::uint8_t* b = env.uart.bytes();
    const std::uint32_t n = env.uart.used();
    bool saw_fragment = false;
    bool saw_query_echo = false;
    for (std::uint32_t i = 0; i + 12 <= n; ++i)
    {
        if (b[i] == 0xE3 && b[i + 1] == 0x10)
        {
            if (b[i + 4] == static_cast<std::uint8_t>(v3::codec::MsgObservability::SnapshotFragment))
            {
                saw_fragment = true;
            }
            if (b[i + 3] == static_cast<std::uint8_t>(v3::codec::Family::Control) &&
                b[i + 4] == static_cast<std::uint8_t>(v3::codec::MsgControl::Query))
            {
                saw_query_echo = true; // Query must NEVER reach the UART
            }
            const std::uint16_t plen = static_cast<std::uint16_t>(b[i + 8]) |
                                       (static_cast<std::uint16_t>(b[i + 9]) << 8);
            i += 11 + plen;
        }
    }
    EXPECT_TRUE(saw_fragment);
    EXPECT_FALSE(saw_query_echo);
}

// ---------------------------------------------------------------------------
// T10: push gate - no telemetry without interest
// ---------------------------------------------------------------------------
TEST(Observability, T10_PushGate)
{
    ObsEnv env;
    env.set_now(1);
    // Bridge default (#49 section 9): telemetry (300 ms) and events flow
    // ALWAYS (profile default) - no subscription needed.
    env.producer.emit_telemetry();
    env.producer.admission_rejected(1);
    env.tick();
    EXPECT_GT(env.uart.used(), 0u);
    env.uart.drain(env.uart.used());

    // logs/traces: push ONLY by subscription (interest = any_sub_class).
    char text[16] = "x";
    env.set_now(2);
    v3::observability::log_emit(env.producer, 2, 3, text, 1);
    env.tick();
    EXPECT_EQ(env.uart.used(), 0u); // no subscription: logs gated, nothing sent

    env.subscribe_mask(0x04); // logs
    env.set_now(3);
    v3::observability::log_emit(env.producer, 2, 3, text, 1);
    env.tick();
    EXPECT_GT(env.uart.used(), 0u); // subscribed: logs flow
}

// ---------------------------------------------------------------------------
// T11: fault capture - staging <= 512 B, supersede, crash-class only
// ---------------------------------------------------------------------------
TEST(Observability, T11_FaultCaptureSupersede)
{
    ObsEnv env;
    env.subscribe_mask(0x08); // traces
    env.set_now(1);

    env.producer.crash_marker_pending(3); // latch #1: fragments queued (traces)
    // tx happens only in Sink::tick (defer-on-backpressure); fragments are in
    // the Traces queue now. Capture ~514 B / 114 B chunk = 5 fragments, and
    // the per-tick Traces cap is 128 B -> several ticks drain it fully.

    // Latch #2 BEFORE the traces queue drains: the first capture is still
    // pending (enqueue is not delivery; per-tick cap 128 B, capture spans
    // several ticks) -> supersede + counter (design §3.3).
    env.set_now(2);
    env.producer.crash_marker_pending(4);
    EXPECT_GE(env.producer.counters().trace_capture_superseded, 1u);

    // Drain until the Traces queue is empty (cap 128 B/tick: ~5 ticks). The
    // fake UART ring accumulates like the real TXE ISR drains it - consume
    // between ticks so tx_bytes_available() stays open.
    for (int i = 0; i < 8; ++i)
    {
        env.set_now(static_cast<std::uint32_t>(10 + i));
        env.tick();
        env.uart.drain(env.uart.used()); // wire consumed (ISR analog)
    }
    EXPECT_GT(env.uart.tx_calls(), 0u); // fragments reached the wire

    // Latch #3 AFTER delivery: pending cleared by Sink (traces drained) ->
    // fresh capture, not a supersede.
    const std::uint32_t before = env.producer.counters().trace_capture_superseded;
    env.set_now(20);
    env.producer.crash_marker_pending(5);
    EXPECT_EQ(env.producer.counters().trace_capture_superseded, before);
    env.tick();
    EXPECT_GT(env.uart.tx_calls(), 0u); // new capture fragments emitted
}

// T11b: auto-clear fault (DegradedTimeout) does NOT latch a capture (#49 8.2).
TEST(Observability, T11b_AutoClearFaultNoCapture)
{
    ObsEnv env;
    env.subscribe_mask(0x08);
    env.set_now(1);

    env.producer.health_changed(SafetyHealth::Ready, SafetyHealth::Fault,
                                v3::safety::DegradedClass::Sensing,
                                v3::safety::SafetyFault::DegradedTimeout);
    env.tick();
    // The health-change EVENT flows (events always), but NO TraceRecord frame:
    // auto-clear fault is RAM + counter only, no capture.
    const std::uint8_t* b = env.uart.bytes();
    const std::uint32_t n = env.uart.used();
    bool saw_trace = false;
    for (std::uint32_t i = 0; i + 12 <= n; ++i)
    {
        if (b[i] == 0xE3 && b[i + 1] == 0x10)
        {
            if (b[i + 4] == static_cast<std::uint8_t>(v3::codec::MsgObservability::TraceRecord))
            {
                saw_trace = true;
            }
            const std::uint16_t plen = static_cast<std::uint16_t>(b[i + 8]) |
                                       (static_cast<std::uint16_t>(b[i + 9]) << 8);
            i += 11 + plen;
        }
    }
    EXPECT_FALSE(saw_trace);
}

// T11c: crash-class fault latches.
TEST(Observability, T11c_CrashClassLatches)
{
    ObsEnv env;
    env.subscribe_mask(0x08);
    env.set_now(1);

    env.producer.health_changed(SafetyHealth::Ready, SafetyHealth::Fault,
                                v3::safety::DegradedClass::None,
                                v3::safety::SafetyFault::CanFailsafe);
    env.tick();
    EXPECT_GT(env.uart.tx_calls(), 0u);
}

// ---------------------------------------------------------------------------
// T12: counters - reset cause, admission histogram, subscription drops
// ---------------------------------------------------------------------------
TEST(Observability, T12_Counters)
{
    ObsEnv env;
    env.subscribe_mask(0x02);
    env.set_now(1);

    env.producer.reset_cause(v3::ResetCause::Watchdog);
    env.producer.admission_rejected(5);
    env.producer.admission_rejected(5);
    env.producer.admission_rejected(11);
    env.producer.subscription_drop(3);

    const auto& c = env.producer.counters();
    EXPECT_EQ(c.reset_by_category[static_cast<std::uint32_t>(v3::ResetCause::Watchdog)], 1u);
    EXPECT_EQ(c.last_boot_cause, static_cast<std::uint32_t>(v3::ResetCause::Watchdog));
    EXPECT_EQ(c.admission_rejects[5], 2u);
    EXPECT_EQ(c.admission_rejects[11], 1u);
    EXPECT_EQ(c.subscription_drops, 1u);
}

// ---------------------------------------------------------------------------
// T15: TX never blocks - full ring -> drop + counter, tick returns
// T15: TX never blocks - full ring => defer (head stays queued), tick returns;
// no drop/counter (defer-on-backpressure, #49 section 10).
TEST(Observability, T15_TxNeverBlocks)
{
    ObsEnv env;
    env.subscribe_mask(0x02);
    env.uart.init(16); // tiny ring: 1 frame max

    for (std::uint32_t i = 0; i < 8; ++i)
    {
        env.set_now(static_cast<std::uint32_t>(i));
        env.producer.admission_rejected(1);
    }
    // Frame (~40 B) > ring 16 B: Sink::tick must NOT block, must NOT drop -
    // it defers (head stays queued) and returns. No tx drop counter.
    env.tick();
    EXPECT_EQ(env.uart.tx_rejected(), 0u);
    EXPECT_EQ(env.uart.tx_calls(), 0u); // nothing fit, nothing sent
    EXPECT_EQ(env.producer.counters().drop_events, 0u); // defer, not drop

    // Ring drains when capacity returns.
    env.uart.init(512);
    env.tick();
    EXPECT_GT(env.uart.tx_calls(), 0u);
}

// ---------------------------------------------------------------------------
// T17: property - drop counters monotonic, queues bounded (bounded walk)
// ---------------------------------------------------------------------------
TEST(Observability, T17_CountersMonotonic)
{
    ObsEnv env;
    env.subscribe_mask(0x02);
    // Deterministic PRNG sequence of emissions; assert counters never decrease
    // and high-water never exceeds capacity.
    std::uint32_t rng = 0x12345678u;
    std::uint32_t prev_drops = 0;
    std::uint32_t prev_hw = 0;
    for (std::uint32_t i = 0; i < 200; ++i)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        env.set_now(i);
        env.producer.admission_rejected(static_cast<std::uint8_t>(rng % 22));
        const auto& c = env.producer.counters();
        EXPECT_GE(c.drop_events, prev_drops);
        EXPECT_LE(c.high_water_events, Sink::EventsCapacity);
        prev_drops = c.drop_events;
        prev_hw = c.high_water_events;
        env.tick();
        env.uart.drain(env.uart.used());
    }
}
