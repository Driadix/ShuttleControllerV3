// Shared fakes for the semantic/runtime slice tests (#74; design
// docs/operation-runtime-design-v3.md section 7.2). Deterministic, fixed-size
// capture buffers (no std::vector - rule R1 spirit, dev-only, matching
// tests/common/fakes.h). Tests flip the gate fakes to drive the admission
// matrix (#46 section 8).
#pragma once

#include <cstdint>

#include "domain/codec.h"
#include "domain/ports.h"
#include "domain/runtime.h"

namespace test
{

class FakeEpoch : public v3::EpochSource
{
  public:
    void set(std::uint32_t e) { m_epoch = e; }
    std::uint32_t epoch() const override { return m_epoch; }

  private:
    std::uint32_t m_epoch = 7;
};

class FakeWindow : public v3::WindowSource
{
  public:
    void set(v3::PlatformWindow w) { m_w = w; }
    v3::PlatformWindow window() const override { return m_w; }

  private:
    v3::PlatformWindow m_w = v3::PlatformWindow::Serving;
};

class FakeHealth : public v3::HealthSource
{
  public:
    void set(v3::safety::SafetyHealth h) { m_h = h; }
    v3::safety::SafetyHealth health() const override { return m_h; }

  private:
    v3::safety::SafetyHealth m_h = v3::safety::SafetyHealth::Ready;
};

class FakeProvisioning : public v3::ProvisioningSource
{
  public:
    void set(v3::ProvisioningStatus s) { m_s = s; }
    v3::ProvisioningStatus status() const override { return m_s; }

  private:
    v3::ProvisioningStatus m_s = v3::ProvisioningStatus::Provisioned;
};

// Recording outbound (canonical frames, fixed capture).
class RecordingOutbound : public v3::OutboundControl
{
  public:
    static constexpr std::uint32_t kMaxFrames = 64;
    struct Frame
    {
        v3::codec::QueueClass cls = v3::codec::QueueClass::Control;
        std::uint8_t data[v3::codec::Mtu] = {};
        std::uint16_t len = 0;
    };

    bool enqueue(v3::codec::QueueClass cls, const std::uint8_t* data, std::uint32_t len) override
    {
        if (m_count < kMaxFrames)
        {
            Frame& f = m_frames[m_count++];
            f.cls = cls;
            f.len = len <= v3::codec::Mtu ? static_cast<std::uint16_t>(len)
                                          : static_cast<std::uint16_t>(v3::codec::Mtu);
            for (std::uint16_t i = 0; i < f.len; ++i)
            {
                f.data[i] = data[i];
            }
        }
        return true;
    }

    const Frame& frame(std::uint32_t i) const { return m_frames[i]; }
    std::uint32_t count() const { return m_count; }
    void clear() { m_count = 0; }

  private:
    Frame m_frames[kMaxFrames];
    std::uint32_t m_count = 0;
};

// Recording runtime events (fixed capture, explicit per-kind counters).
class RecordingEvents : public v3::RuntimeEvents
{
  public:
    static constexpr std::uint8_t kMax = 64;
    struct Dup
    {
        std::uint32_t id = 0;
        bool conflict = false;
    };
    struct Started
    {
        std::uint32_t op = 0;
        std::uint16_t type = 0;
    };
    struct Term
    {
        std::uint32_t op = 0;
        std::uint16_t type = 0;
        std::uint16_t code = 0;
    };
    struct Sub
    {
        std::uint16_t authority = 0;
        bool active = false;
    };

    void admission_rejected(std::uint8_t code) override
    {
        if (reject_count < kMax)
        {
            rejects[reject_count++] = code;
        }
    }
    void request_duplicate(std::uint32_t id, bool conflict) override
    {
        if (dup_count < kMax)
        {
            dups[dup_count].id = id;
            dups[dup_count].conflict = conflict;
            ++dup_count;
        }
    }
    void transport_error(v3::codec::TransportError e) override
    {
        if (error_count < kMax)
        {
            errors[error_count++] = static_cast<std::uint8_t>(e);
        }
    }
    void queue_rejected(v3::codec::QueueClass cls) override
    {
        if (queue_reject_count < kMax)
        {
            queue_rejects[queue_reject_count++] = static_cast<std::uint8_t>(cls);
        }
    }
    void operation_started(std::uint32_t op, std::uint16_t t) override
    {
        if (started_count < kMax)
        {
            started[started_count].op = op;
            started[started_count].type = t;
            ++started_count;
        }
    }
    void operation_terminal(std::uint32_t op, std::uint16_t t, std::uint16_t code) override
    {
        if (terminal_count < kMax)
        {
            terminal[terminal_count].op = op;
            terminal[terminal_count].type = t;
            terminal[terminal_count].code = code;
            ++terminal_count;
        }
    }
    void subscription_changed(std::uint16_t a, bool active) override
    {
        if (sub_count < kMax)
        {
            subs[sub_count].authority = a;
            subs[sub_count].active = active;
            ++sub_count;
        }
    }
    void subscription_drop(std::uint8_t id) override
    {
        if (drop_count < kMax)
        {
            drops[drop_count++] = id;
        }
    }

    void clear()
    {
        reject_count = 0;
        dup_count = 0;
        error_count = 0;
        queue_reject_count = 0;
        started_count = 0;
        terminal_count = 0;
        sub_count = 0;
        drop_count = 0;
    }

    std::uint8_t rejects[kMax] = {};
    std::uint8_t errors[kMax] = {};
    std::uint8_t queue_rejects[kMax] = {};
    std::uint8_t drops[kMax] = {};
    Dup dups[kMax] = {};
    Started started[kMax] = {};
    Term terminal[kMax] = {};
    Sub subs[kMax] = {};
    std::uint8_t reject_count = 0;
    std::uint8_t error_count = 0;
    std::uint8_t queue_reject_count = 0;
    std::uint8_t drop_count = 0;
    std::uint8_t dup_count = 0;
    std::uint8_t started_count = 0;
    std::uint8_t terminal_count = 0;
    std::uint8_t sub_count = 0;
};

// --- Frame building helpers (fixed buffers, no allocation) -------------------

// Serializes an OperationRequest payload (little-endian, mirrors the codec).
inline std::uint16_t request_payload(const v3::codec::OperationRequest& r, std::uint8_t* out, std::uint16_t cap)
{
    const std::uint16_t need = static_cast<std::uint16_t>(18 + r.params_len);
    if (cap < need)
    {
        return 0;
    }
    auto wr16 = [out](std::size_t o, std::uint16_t v) {
        out[o] = static_cast<std::uint8_t>(v & 0xFFu);
        out[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    };
    auto wr32 = [out](std::size_t o, std::uint32_t v) {
        out[o] = static_cast<std::uint8_t>(v & 0xFFu);
        out[o + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        out[o + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        out[o + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    };
    wr32(0, r.request_id);
    wr32(4, r.controller_epoch);
    wr16(8, r.authority_id);
    out[10] = r.role;
    wr16(11, r.operation_type);
    wr32(13, r.parent_operation_id);
    out[17] = r.params_len;
    for (std::uint8_t i = 0; i < r.params_len; ++i)
    {
        out[18 + i] = r.params[i];
    }
    return need;
}

// Builds a canonical Control frame for msg_type with the given payload.
inline std::uint16_t make_control_frame(std::uint8_t msg_type, const std::uint8_t* payload,
                                        std::uint16_t payload_len, std::uint8_t* out, std::uint16_t cap)
{
    v3::codec::Header h;
    h.msg_family = static_cast<std::uint8_t>(v3::codec::Family::Control);
    h.msg_type = msg_type;
    h.queue_class = static_cast<std::uint8_t>(v3::codec::QueueClass::Control);
    return v3::codec::encode(out, cap, h, payload, payload_len);
}

// Builds a canonical OperationRequest frame.
inline std::uint16_t request_frame(const v3::codec::OperationRequest& r, std::uint8_t* out, std::uint16_t cap)
{
    std::uint8_t payload[v3::codec::MaxPayload];
    const std::uint16_t plen = request_payload(r, payload, sizeof(payload));
    if (plen == 0)
    {
        return 0;
    }
    return make_control_frame(static_cast<std::uint8_t>(v3::codec::MsgControl::OperationRequest),
                              payload, plen, out, cap);
}

// --- Shared operation drivers (#74 shell) ------------------------------------

// Immediately completes with code 100.
inline v3::runtime::DriverEvent driver_complete(void*, const v3::runtime::OperationEnv&)
{
    v3::runtime::DriverEvent e;
    e.kind = v3::runtime::DriverEventKind::Complete;
    e.outcome.code = 100;
    return e;
}

// Returns Continue forever (stays Running).
inline v3::runtime::DriverEvent driver_continue(void*, const v3::runtime::OperationEnv&)
{
    v3::runtime::DriverEvent e;
    e.kind = v3::runtime::DriverEventKind::Continue;
    return e;
}

// Returns Yield forever (parks; stays Running).
inline v3::runtime::DriverEvent driver_yield(void*, const v3::runtime::OperationEnv&)
{
    v3::runtime::DriverEvent e;
    e.kind = v3::runtime::DriverEventKind::Yield;
    return e;
}

// Fails immediately with code 2.
inline v3::runtime::DriverEvent driver_fail(void*, const v3::runtime::OperationEnv&)
{
    v3::runtime::DriverEvent e;
    e.kind = v3::runtime::DriverEventKind::Fail;
    e.outcome.code = 2;
    return e;
}

// Continue while Running, Cancel (code 1) once stop is requested (#13).
inline v3::runtime::DriverEvent driver_cancel_on_stop(void*, const v3::runtime::OperationEnv& env)
{
    v3::runtime::DriverEvent e;
    if (env.stop_requested)
    {
        e.kind = v3::runtime::DriverEventKind::Cancel;
        e.outcome.code = 1;
    }
    else
    {
        e.kind = v3::runtime::DriverEventKind::Continue;
    }
    return e;
}

// Continue while Running, Fail (code 3) once stop is requested (safe stop
// could not complete -> Stopping -> Failed, #13).
inline v3::runtime::DriverEvent driver_fail_on_stop(void*, const v3::runtime::OperationEnv& env)
{
    v3::runtime::DriverEvent e;
    if (env.stop_requested)
    {
        e.kind = v3::runtime::DriverEventKind::Fail;
        e.outcome.code = 3;
    }
    else
    {
        e.kind = v3::runtime::DriverEventKind::Continue;
    }
    return e;
}

// Spawns one child of type 2 (driver_complete) then completes when the child
// outcome arrives. State visible through the context.
struct SpawnCtx
{
    bool spawned = false;
    bool saw_child_terminal = false;
    std::uint16_t child_code = 0;
};

inline v3::runtime::DriverEvent driver_spawn_once(void* ctx, const v3::runtime::OperationEnv& env)
{
    auto* c = static_cast<SpawnCtx*>(ctx);
    v3::runtime::DriverEvent e;
    if (!c->spawned)
    {
        c->spawned = true;
        e.kind = v3::runtime::DriverEventKind::Spawn;
        e.spawn_type = 2;
        e.spawn_fn = driver_complete; // child driver (Phase 3: type registry)
        e.spawn_ctx = nullptr;
        return e;
    }
    if (env.child_terminal)
    {
        c->saw_child_terminal = true;
        c->child_code = env.child_outcome.code;
        e.kind = v3::runtime::DriverEventKind::Complete;
        e.outcome.code = 7;
        return e;
    }
    e.kind = v3::runtime::DriverEventKind::Yield;
    return e;
}

// Spawns TWO children (types 2 and 3, driver_complete), then completes with
// code 7 once a child outcome arrives. Multi-child deferred-terminal path.
struct SpawnTwoCtx
{
    std::uint8_t spawned = 0;
    bool saw_child_terminal = false;
};

inline v3::runtime::DriverEvent driver_spawn_two(void* ctx, const v3::runtime::OperationEnv& env)
{
    auto* c = static_cast<SpawnTwoCtx*>(ctx);
    v3::runtime::DriverEvent e;
    if (c->spawned == 0)
    {
        c->spawned = 1;
        e.kind = v3::runtime::DriverEventKind::Spawn;
        e.spawn_type = 2;
        e.spawn_fn = driver_complete;
        e.spawn_ctx = nullptr;
        return e;
    }
    if (c->spawned == 1)
    {
        c->spawned = 2;
        e.kind = v3::runtime::DriverEventKind::Spawn;
        e.spawn_type = 3;
        e.spawn_fn = driver_complete;
        e.spawn_ctx = nullptr;
        return e;
    }
    if (env.child_terminal)
    {
        c->saw_child_terminal = true;
        e.kind = v3::runtime::DriverEventKind::Complete;
        e.outcome.code = 7;
        return e;
    }
    e.kind = v3::runtime::DriverEventKind::Yield;
    return e;
}

// Counts invocations (for the one-instance-per-call boundedness check).
inline v3::runtime::DriverEvent driver_count(void* ctx, const v3::runtime::OperationEnv&)
{
    auto* n = static_cast<std::uint32_t*>(ctx);
    ++*n;
    v3::runtime::DriverEvent e;
    e.kind = v3::runtime::DriverEventKind::Continue;
    return e;
}

} // namespace test
