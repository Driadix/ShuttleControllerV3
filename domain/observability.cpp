// Observability Producer + Sink implementation (design
// docs/observability-design-v3.md sections 3, 6; ticket #72). Framework-free.
#include "domain/observability.h"

namespace
{

// Event registry ranges (#49 section 5). Concrete ids assigned additively
// (registry contract #47 item 6).
constexpr std::uint16_t kEvHealthChanged = 0x0200;          // Health
constexpr std::uint16_t kEvAdmissionRejected = 0x0400;      // Admission
constexpr std::uint16_t kEvRequestDuplicate = 0x0401;
constexpr std::uint16_t kEvTransportError = 0x0500;         // Queue/Overload
constexpr std::uint16_t kEvQueueRejected = 0x0501;
constexpr std::uint16_t kEvClassDrop = 0x0502;
constexpr std::uint16_t kEvTraceCaptureSuperseded = 0x0504;
constexpr std::uint16_t kEvOperationStarted = 0x0600;       // Operations
constexpr std::uint16_t kEvOperationTerminal = 0x0601;
constexpr std::uint16_t kEvSubscriptionChanged = 0x0602;
constexpr std::uint16_t kEvSubscriptionDrop = 0x0603;
constexpr std::uint16_t kEvStopIssued = 0x0604;
constexpr std::uint16_t kEvCanFailsafe = 0x0605;
constexpr std::uint16_t kEvCrashMarkerPending = 0x0800;     // Boot/Reset
constexpr std::uint16_t kEvResetCause = 0x0801;
constexpr std::uint16_t kEvStepOverrun = 0x0802;
constexpr std::uint16_t kEvSchedulerGap = 0x0803;
constexpr std::uint16_t kEvScheduleRejected = 0x0804;

// Severity codes (design 2.2): info/warning/error/fatal.
constexpr std::uint8_t kSevInfo = 0;
constexpr std::uint8_t kSevWarning = 1;
constexpr std::uint8_t kSevError = 2;
constexpr std::uint8_t kSevFatal = 3;

} // namespace

namespace v3
{
namespace observability
{

bool is_crash_class(safety::SafetyFault f)
{
    // #49 section 8.2 / #45 section 5: only non-auto-clear faults latch a
    // capture; auto-clear (DegradedTimeout, DirectionalToF) are RAM + counter.
    return f == safety::SafetyFault::CanFailsafe || f == safety::SafetyFault::CrashMarker;
}

void Producer::init(EpochSource* epoch, WindowSource* window, HealthSource* health,
                    ProvisioningSource* prov, subscription::Registry* subs,
                    runtime::Runtime* rt, sensing::SensingView* sensing,
                    SafetyDiag* diag, WallClockSource* wall, IdentitySource* identity,
                    Sink* sink)
{
    m_epoch = epoch;
    m_window = window;
    m_health = health;
    m_prov = prov;
    m_subs = subs;
    m_rt = rt;
    m_sensing = sensing;
    m_diag = diag;
    m_wall = wall;
    m_identity = identity;
    m_sink = sink;
    m_initialized = true;
}

void Producer::bump_counter(codec::QueueClass cls)
{
    switch (cls)
    {
    case codec::QueueClass::Telemetry:
        ++m_counters.drop_telemetry;
        break;
    case codec::QueueClass::Events:
        ++m_counters.drop_events;
        break;
    case codec::QueueClass::Logs:
        ++m_counters.drop_logs;
        break;
    case codec::QueueClass::Traces:
        ++m_counters.drop_traces;
        break;
    default:
        break; // Control/Service: no per-class drop counter (bounded answers)
    }
}

void Producer::note_class_drop(codec::QueueClass cls)
{
    if (!m_initialized)
    {
        return;
    }
    bump_counter(cls); // counter first: never lost even under event backpressure
    // Coalesced pending event: emitted by flush_pending_drop() once the Events
    // queue has capacity again (defer-on-backpressure, #49 section 10). No
    // synchronous enqueue here - the Events queue may be full (recursion).
    m_pending_drop_cls = static_cast<std::uint8_t>(cls);
    m_pending_drop = true;
}

void Producer::flush_pending_drop()
{
    if (!m_initialized || !m_pending_drop)
    {
        return;
    }
    // Only flush when the Events queue has capacity: Sink queries its queue
    // (events are never dropped-newest for the flush itself).
    if (m_sink != nullptr && !m_sink->events_full())
    {
        const codec::QueueClass cls = static_cast<codec::QueueClass>(m_pending_drop_cls);
        m_pending_drop = false;
        emit_event(kEvClassDrop, kSevWarning, static_cast<std::uint8_t>(cls),
                   static_cast<std::uint32_t>(cls), 0);
    }
}

void Producer::note_high_water(codec::QueueClass cls, std::uint32_t size)
{
    if (!m_initialized)
    {
        return;
    }
    const auto cap = static_cast<std::uint16_t>(size > 0xFFFFu ? 0xFFFFu : size);
    switch (cls)
    {
    case codec::QueueClass::Telemetry:
        if (cap > m_counters.high_water_telemetry)
        {
            m_counters.high_water_telemetry = cap;
        }
        break;
    case codec::QueueClass::Events:
        if (cap > m_counters.high_water_events)
        {
            m_counters.high_water_events = cap;
        }
        break;
    case codec::QueueClass::Logs:
        if (cap > m_counters.high_water_logs)
        {
            m_counters.high_water_logs = cap;
        }
        break;
    case codec::QueueClass::Traces:
        if (cap > m_counters.high_water_traces)
        {
            m_counters.high_water_traces = cap;
        }
        break;
    default:
        break;
    }
}

// --- Low-level emission -----------------------------------------------------

void Producer::emit_class(codec::QueueClass cls, const std::uint8_t* body,
                          std::uint16_t body_len, bool with_wall,
                          std::uint16_t event_id, std::uint8_t severity)
{
    if (!m_initialized || m_sink == nullptr)
    {
        return;
    }
    // Push gate (#49 section 9): events always (reserve); telemetry/logs/traces
    // only with interest (profile default or an active subscription). Pull
    // (Query) is unaffected - it uses the snapshot path, not emit_class.
    if (cls != codec::QueueClass::Events && m_subs != nullptr &&
        !m_subs->interest(cls))
    {
        return;
    }
    codec::Envelope env;
    env.class_id = static_cast<std::uint8_t>(cls);
    env.monotonic_tick = m_now_ms;
    if (m_epoch != nullptr)
    {
        env.controller_epoch = m_epoch->epoch();
    }
    env.time_validity = m_wall != nullptr ? m_wall->time_validity() : codec::TimeValidity::Unsynced;
    env.wall_time = env.time_validity != codec::TimeValidity::Unsynced && m_wall != nullptr
                        ? m_wall->epoch_sec()
                        : 0u;
    env.seq = next_seq(cls);

    std::uint8_t payload[codec::MaxPayload];
    std::uint16_t n = codec::encode_envelope(payload, sizeof(payload), env, with_wall);
    if (n == 0 || n + body_len > sizeof(payload))
    {
        return; // cannot happen with bounded bodies; defensive
    }
    for (std::uint16_t i = 0; i < body_len; ++i)
    {
        payload[n + i] = body[i];
    }
    n = static_cast<std::uint16_t>(n + body_len);

    codec::Header h;
    h.msg_family = static_cast<std::uint8_t>(codec::Family::Observability);
    h.msg_type = static_cast<std::uint8_t>(record_type(cls));
    h.queue_class = static_cast<std::uint8_t>(cls);
    h.flags = event_id != 0 ? codec::FlagReserve : 0; // reserved-flag hint (unused now)
    std::uint8_t frame[codec::Mtu];
    const std::uint16_t flen = codec::encode(frame, sizeof(frame), h, payload, n);
    if (flen > 0)
    {
        (void)m_sink->enqueue(cls, frame, flen);
    }
}

void Producer::emit_event(std::uint16_t event_id, std::uint8_t severity,
                          std::uint8_t ctx_kind, std::uint32_t ctx_value,
                          std::uint32_t ctx_value2)
{
    if (!m_initialized)
    {
        return;
    }
    codec::EventBody b;
    b.event_id = event_id;
    b.severity = severity;
    b.ctx_kind = ctx_kind;
    b.ctx_value = ctx_value;
    b.ctx_value2 = ctx_value2;
    std::uint8_t body[codec::MaxPayload];
    const std::uint16_t blen = codec::encode_event_body(body, sizeof(body), b);
    emit_class(codec::QueueClass::Events, body, blen, /*with_wall=*/true, event_id, severity);
}

// --- KernelEvents -----------------------------------------------------------

void Producer::step_overrun(std::uint32_t step_ms)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvStepOverrun, kSevError, 0, step_ms, 0);
}

void Producer::scheduler_gap(std::uint64_t gap_ms)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvSchedulerGap, kSevWarning, 0,
               static_cast<std::uint32_t>(gap_ms > 0xFFFFFFFFull ? 0xFFFFFFFFull : gap_ms), 0);
}

void Producer::schedule_rejected()
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvScheduleRejected, kSevError, 0, 0, 0);
}

void Producer::reset_cause(ResetCause cause)
{
    if (!m_initialized)
    {
        return;
    }
    m_boot_cause = cause;
    m_boot_cause_set = true;
    if (static_cast<std::uint32_t>(cause) < 5u)
    {
        ++m_counters.reset_by_category[static_cast<std::uint32_t>(cause)];
    }
    m_counters.last_boot_cause = static_cast<std::uint32_t>(cause);
    emit_event(kEvResetCause, kSevInfo, static_cast<std::uint8_t>(cause),
               static_cast<std::uint32_t>(cause), 0);
    // Crash-class boot (design §3.3 / #49 §8.2): a non-power-on reset cause
    // latches a fault capture (watchdog/software/external/unknown = reboot
    // class; PowerOn is the normal boot, no capture).
    if (cause != ResetCause::PowerOn)
    {
        start_fault_capture(kEvResetCause, m_now_ms);
    }
}

// --- RuntimeEvents ----------------------------------------------------------

void Producer::admission_rejected(std::uint8_t reject_code)
{
    if (!m_initialized)
    {
        return;
    }
    if (reject_code < 22u)
    {
        ++m_counters.admission_rejects[reject_code];
    }
    emit_event(kEvAdmissionRejected, kSevWarning, reject_code,
               static_cast<std::uint32_t>(reject_code), 0);
}

void Producer::request_duplicate(std::uint32_t request_id, bool conflict)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvRequestDuplicate, conflict ? kSevError : kSevInfo, conflict ? 1u : 0u,
               request_id, 0);
}

void Producer::transport_error(codec::TransportError e)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvTransportError, kSevWarning, static_cast<std::uint8_t>(e),
               static_cast<std::uint32_t>(e), 0);
}

void Producer::queue_rejected(codec::QueueClass cls)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvQueueRejected, kSevWarning, static_cast<std::uint8_t>(cls),
               static_cast<std::uint32_t>(cls), 0);
}

void Producer::operation_started(std::uint32_t op_id, std::uint16_t type_id)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvOperationStarted, kSevInfo, 0, op_id, static_cast<std::uint32_t>(type_id));
}

void Producer::operation_terminal(std::uint32_t op_id, std::uint16_t type_id,
                                  std::uint16_t outcome_code)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvOperationTerminal, kSevInfo, 0, op_id,
               (static_cast<std::uint32_t>(type_id) << 16) | static_cast<std::uint32_t>(outcome_code));
}

void Producer::subscription_changed(std::uint16_t authority_id, bool active)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvSubscriptionChanged, kSevInfo, active ? 1u : 0u,
               static_cast<std::uint32_t>(authority_id), 0);
}

void Producer::subscription_drop(std::uint8_t sub_id)
{
    if (!m_initialized)
    {
        return;
    }
    ++m_counters.subscription_drops;
    emit_event(kEvSubscriptionDrop, kSevWarning, 0, static_cast<std::uint32_t>(sub_id), 0);
}

// --- safety::Events ---------------------------------------------------------

void Producer::health_changed(safety::SafetyHealth from, safety::SafetyHealth to,
                              safety::DegradedClass cls, safety::SafetyFault fault)
{
    if (!m_initialized)
    {
        return;
    }
    const std::uint8_t sev = to == safety::SafetyHealth::Fault ? kSevError
                             : to == safety::SafetyHealth::Degraded   ? kSevWarning
                                                                      : kSevInfo;
    emit_event(kEvHealthChanged, sev, static_cast<std::uint8_t>(from),
               static_cast<std::uint32_t>(to),
               (static_cast<std::uint32_t>(cls) << 16) | static_cast<std::uint32_t>(fault));
    // Crash-class fault latch (#49 section 8.2; non-auto-clear only).
    if (to == safety::SafetyHealth::Fault && is_crash_class(fault))
    {
        start_fault_capture(kEvHealthChanged, m_now_ms);
    }
}

void Producer::stop_issued(safety::StopProfile profile, std::uint32_t seq)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvStopIssued, kSevWarning, static_cast<std::uint8_t>(profile), seq, 0);
}

void Producer::can_failsafe(CanErrorState state)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvCanFailsafe, kSevError, static_cast<std::uint8_t>(state),
               static_cast<std::uint32_t>(state), 0);
}

void Producer::crash_marker_pending(std::uint32_t crash_count)
{
    if (!m_initialized)
    {
        return;
    }
    emit_event(kEvCrashMarkerPending, kSevFatal, 0, crash_count, 0);
    start_fault_capture(kEvCrashMarkerPending, m_now_ms);
}

// --- Telemetry --------------------------------------------------------------

void Producer::emit_telemetry()
{
    if (!m_initialized || m_subs == nullptr || m_sink == nullptr)
    {
        return;
    }
    if (!m_subs->interest(codec::QueueClass::Telemetry))
    {
        return; // push gated by subscription / profile default (#49 section 9)
    }
    codec::TelemetryBody b;
    if (m_health != nullptr)
    {
        b.health = static_cast<std::uint8_t>(m_health->health());
    }
    if (m_diag != nullptr)
    {
        b.fault_mask = fault_mask_from_diag();
        b.warning_mask = warning_mask_from_diag();
        b.op_state = op_state_from_diag();
    }
    if (m_sensing != nullptr)
    {
        // position placeholder 0 until Phase 3 motion; freshness not carried.
    }
    std::uint8_t body[codec::MaxPayload];
    const std::uint16_t blen = codec::encode_telemetry_body(body, sizeof(body), b);
    emit_class(codec::QueueClass::Telemetry, body, blen, /*with_wall=*/false, 0, kSevInfo);
}

// --- Fault capture ----------------------------------------------------------

void Producer::start_fault_capture(std::uint16_t trigger_event_id, std::uint32_t trigger_tick)
{
    if (!m_initialized || m_sink == nullptr)
    {
        return;
    }
    // A capture is pending while its trace fragments are still queued (not yet
    // drained to the UART - per-tick cap 128 B, a ~489 B capture spans several
    // ticks). A second crash-class latch in that window supersedes the old
    // capture (design §3.3, #49 §8.2): the old staging is obsolete, its
    // fragments would interleave with the new ones. Enqueue is NOT delivery -
    // the flag clears only via Sink::on_capture_delivered (traces drained).
    if (m_capture_pending)
    {
        ++m_counters.trace_capture_superseded;
        emit_event(kEvTraceCaptureSuperseded, kSevWarning, 0, 0, 0);
        // Drop the superseded capture's still-queued fragments (design §3.3):
        // they are obsolete and must not interleave with the new capture.
        m_sink->drop_pending_capture();
    }

    std::uint16_t n = 0;
    // Trigger header (11 B) + reset cause (1 B) + compact snapshot block + tails.
    codec::TraceBodyHeader th;
    th.kind = 0;
    th.trigger_event_id = trigger_event_id;
    th.trigger_tick = trigger_tick;
    th.payload_len = 0; // filled after assembly
    n = codec::encode_trace_header(m_staging, sizeof(m_staging), th);
    if (n == 0)
    {
        return;
    }
    m_staging[n++] = static_cast<std::uint8_t>(m_boot_cause);

    // Compact snapshot block (design 2.2: fragment of snapshot - health/window/
    // epoch; ~20 B, NOT the full document - the full doc would overflow 512 B).
    const auto put8c = [&](std::uint8_t v) {
        if (n < sizeof(m_staging))
        {
            m_staging[n++] = v;
        }
    };
    const auto put32c = [&](std::uint32_t v) {
        if (n + 4 > sizeof(m_staging))
        {
            return;
        }
        m_staging[n++] = static_cast<std::uint8_t>(v & 0xFFu);
        m_staging[n++] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        m_staging[n++] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        m_staging[n++] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    };
    put32c(m_epoch != nullptr ? m_epoch->epoch() : 0u);
    put8c(m_window != nullptr ? static_cast<std::uint8_t>(m_window->window()) : 0u);
    put8c(m_health != nullptr ? static_cast<std::uint8_t>(m_health->health()) : 0u);
    put8c(m_diag != nullptr ? static_cast<std::uint8_t>(m_diag->fault) : 0u);
    put8c(m_diag != nullptr ? static_cast<std::uint8_t>(m_diag->degraded_class) : 0u);
    put32c(m_diag != nullptr ? m_diag->uptime_ms : 0u);
    // (4+1+1+1+1+1+4 = 13 B)

    // Tails (events 8 x 26 B, logs 8 x 32 B) - compact, bounded to 512 B total:
    // 11 (header) + 1 (reset) + 13 (snapshot) + 208 + 256 = 489 <= 512.
    const std::uint16_t ev = m_sink->tail_events(m_staging + n, static_cast<std::uint16_t>(sizeof(m_staging) - n));
    n = static_cast<std::uint16_t>(n + ev);
    const std::uint16_t lg = m_sink->tail_logs(m_staging + n, static_cast<std::uint16_t>(sizeof(m_staging) - n));
    n = static_cast<std::uint16_t>(n + lg);

    m_staging_len = n;
    // Pending stays TRUE until the capture fragments have actually drained to
    // the UART (Sink::on_capture_delivered clears it): a second latch before
    // that point supersedes this capture (design §3.3). Enqueue is not delivery.
    m_capture_pending = true;

    // Deliver as fragmented traces records (design 3.3): the capture stream =
    // envelope (14) + TraceBodyHeader (11) + staging (<= 489); each fragment
    // payload = { fragmentIndex u8, fragmentCount u8, chunk[] } <= 114 B,
    // canonical frame family=Observability msg_type=TraceRecord queue_class=Traces.
    // Per-tick cap 128 B drains at most one fragment per tick (several ticks).
    send_trace_capture(trigger_event_id, trigger_tick, m_staging, m_staging_len);
    // m_capture_pending remains true; the Sink reports drain completion.
    m_sink->note_capture_pending();
}

void Producer::on_capture_delivered()
{
    // Called by the Sink when the Traces queue has drained empty after a
    // capture: the capture is fully on the wire; a future latch is a fresh
    // capture, not a supersede (design §3.3).
    m_capture_pending = false;
}

// --- Snapshot ---------------------------------------------------------------

void Producer::assemble_snapshot(std::uint8_t sections_mask, std::uint8_t* out,
                                 std::uint16_t& out_len)
{
    // Bounded document: version, epoch, wall, window/health/provisioning,
    // identity, operation summary, counters (design 2.3). Sections beyond the
    // sources available in #72 are zero (identity fw/build/serial placeholders).
    std::uint16_t n = 0;
    const auto put32 = [&](std::uint32_t v) {
        if (n + 4 <= 456u)
        {
            out[n++] = static_cast<std::uint8_t>(v & 0xFFu);
            out[n++] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
            out[n++] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
            out[n++] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
        }
    };
    const auto put8 = [&](std::uint8_t v) {
        if (n + 1 <= 456u)
        {
            out[n++] = v;
        }
    };

    ++m_doc_version;
    // Document header: version + controller_epoch ALWAYS precede, independent
    // of sections_mask (design §2.3 fencing: stale documents are discarded by
    // version, epoch fences reboots; AWS Shadow pattern).
    put32(m_doc_version);
    put32(m_epoch != nullptr ? m_epoch->epoch() : 0u);
    // Sections are selected by sections_mask (design §2.3/§3.2): base (0x01),
    // gates (0x02), identity (0x04), ops (0x08), sensing (0x10), actuator (0x20),
    // masks (0x40), counters (0x80). 0xFF = all. Sections absent from the mask
    // are skipped (bounded doc).
    const bool want_base = (sections_mask & 0x01) != 0;
    const bool want_gates = (sections_mask & 0x02) != 0;
    const bool want_identity = (sections_mask & 0x04) != 0;
    const bool want_ops = (sections_mask & 0x08) != 0;
    const bool want_sensing = (sections_mask & 0x10) != 0;
    const bool want_actuator = (sections_mask & 0x20) != 0;
    const bool want_masks = (sections_mask & 0x40) != 0;
    const bool want_counters = (sections_mask & 0x80) != 0;

    // Base: wall + validity.
    if (want_base)
    {
        if (m_wall != nullptr)
        {
            put32(m_wall->epoch_sec());
            put8(static_cast<std::uint8_t>(m_wall->time_validity()));
        }
        else
        {
            put32(0);
            put8(0);
        }
    }
    // Gates: window/health/fault/degraded/provisioning + profile stub.
    if (want_gates)
    {
        put8(m_window != nullptr ? static_cast<std::uint8_t>(m_window->window()) : 0u);
        put8(m_health != nullptr ? static_cast<std::uint8_t>(m_health->health()) : 0u);
        put8(m_diag != nullptr ? static_cast<std::uint8_t>(m_diag->fault) : 0u);
        put8(m_diag != nullptr ? static_cast<std::uint8_t>(m_diag->degraded_class) : 0u);
        put8(m_prov != nullptr ? static_cast<std::uint8_t>(m_prov->status()) : 0u);
        // Profile stub (#59): supported {800,1000,1200}, qualified/configured/active 0.
        for (std::uint8_t i = 0; i < 12; ++i)
        {
            put8(0);
        }
    }
    // Identity: hardware_id (STM32 UID high 32), fw/build/serial placeholders 0.
    if (want_identity)
    {
        put32(m_identity != nullptr ? m_identity->hardware_id() : 0u);
        put32(0);
        put32(0);
        put32(0);
    }
    // Operation summary (runtime snapshot, <= 8).
    if (want_ops)
    {
        if (m_rt != nullptr)
        {
            std::uint32_t count = 0;
            const runtime::Instance* inst = m_rt->snapshot(count);
            put8(count > 8 ? 8u : static_cast<std::uint8_t>(count));
            const std::uint32_t show = count > 8 ? 8u : count;
            for (std::uint32_t i = 0; i < show; ++i)
            {
                put32(inst[i].op_id);
                put8(static_cast<std::uint8_t>(inst[i].type_id & 0xFFu));
                put8(static_cast<std::uint8_t>((inst[i].type_id >> 8) & 0xFFu)); // u16, design §2.3
                put8(static_cast<std::uint8_t>(inst[i].state));
                put8(static_cast<std::uint8_t>(inst[i].activity));
            }
        }
        else
        {
            put8(0);
        }
    }
    // Sensing: 5 sensors {raw u32, age u32, state u8} = 45 B.
    if (want_sensing)
    {
        if (m_sensing != nullptr)
        {
            for (std::uint32_t s = 0; s < 5u; ++s)
            {
                sensing::SensorSnapshot snap;
                if (m_sensing->get_snapshot(static_cast<sensing::SensorId>(s), &snap))
                {
                    put32(snap.raw);
                    put32(snap.age_ms);
                    put8(static_cast<std::uint8_t>(snap.state));
                }
                else
                {
                    put32(0);
                    put32(0);
                    put8(0);
                }
            }
        }
    }
    // Actuator commanded (current intent) + battery reserved.
    if (want_actuator)
    {
        if (m_diag != nullptr)
        {
            put32(m_diag->current_intent.seq);
            put32(static_cast<std::uint32_t>(m_diag->current_intent.kind));
        }
        else
        {
            put32(0);
            put32(0);
        }
        for (std::uint8_t i = 0; i < 8; ++i)
        {
            put8(0); // battery reserved
        }
    }
    // Fault/warning masks.
    if (want_masks)
    {
        put8(static_cast<std::uint8_t>(m_diag != nullptr ? fault_mask_from_diag() & 0xFFu : 0u));
        put8(static_cast<std::uint8_t>(m_diag != nullptr ? (fault_mask_from_diag() >> 8) & 0xFFu : 0u));
        put8(static_cast<std::uint8_t>(m_diag != nullptr ? warning_mask_from_diag() & 0xFFu : 0u));
        put8(static_cast<std::uint8_t>(m_diag != nullptr ? (warning_mask_from_diag() >> 8) & 0xFFu : 0u));
    }
    // Counters compact slice (uptime, drops, reset histogram).
    if (want_counters)
    {
        put32(m_counters.uptime_s);
        put16_drop(out, n, m_counters.drop_telemetry, m_counters.drop_events);
        put16_drop(out, n, m_counters.drop_logs, m_counters.drop_traces);
    }
    out_len = n;
}

void Producer::send_snapshot_fragments(const std::uint8_t* doc, std::uint16_t doc_len,
                                       codec::QueueClass cls)
{
    if (m_sink == nullptr)
    {
        return;
    }
    const std::uint16_t chunk = codec::MaxPayload - 2; // fragmentIndex + fragmentCount
    std::uint16_t frag_count = static_cast<std::uint16_t>((doc_len + chunk - 1) / chunk);
    if (frag_count == 0)
    {
        frag_count = 1;
    }
    if (frag_count > 4)
    {
        frag_count = 4; // bridge bounded answer (#49 section 10)
    }
    std::uint16_t sent = 0;
    for (std::uint16_t f = 0; f < frag_count; ++f)
    {
        std::uint8_t payload[codec::MaxPayload];
        payload[0] = static_cast<std::uint8_t>(f);
        payload[1] = static_cast<std::uint8_t>(frag_count);
        std::uint16_t take = doc_len - sent;
        if (take > chunk)
        {
            take = chunk;
        }
        std::uint16_t m = 2;
        for (std::uint16_t i = 0; i < take; ++i)
        {
            payload[m++] = doc[sent + i];
        }
        sent = static_cast<std::uint16_t>(sent + take);

        codec::Header h;
        h.msg_family = static_cast<std::uint8_t>(codec::Family::Observability);
        h.msg_type = static_cast<std::uint8_t>(codec::MsgObservability::SnapshotFragment);
        h.queue_class = static_cast<std::uint8_t>(cls);
        std::uint8_t frame[codec::Mtu];
        const std::uint16_t flen = codec::encode(frame, sizeof(frame), h, payload, m);
        if (flen > 0)
        {
            (void)m_sink->enqueue(cls, frame, flen);
        }
    }
}

void Producer::send_trace_capture(std::uint16_t trigger_event_id, std::uint32_t trigger_tick,
                                  const std::uint8_t* staging, std::uint16_t staging_len)
{
    if (m_sink == nullptr || !m_initialized)
    {
        return;
    }
    // Capture stream = envelope (14 B, with wall) + TraceBodyHeader (11 B) + staging.
    codec::Envelope env;
    env.class_id = static_cast<std::uint8_t>(codec::QueueClass::Traces);
    env.monotonic_tick = m_now_ms;
    env.controller_epoch = m_epoch != nullptr ? m_epoch->epoch() : 0u;
    env.time_validity = m_wall != nullptr ? m_wall->time_validity() : codec::TimeValidity::Unsynced;
    env.wall_time = env.time_validity != codec::TimeValidity::Unsynced && m_wall != nullptr
                        ? m_wall->epoch_sec()
                        : 0u;
    env.seq = m_seq_traces++;
    std::uint8_t head[codec::MaxPayload];
    std::uint16_t hn = codec::encode_envelope(head, sizeof(head), env, /*with_wall=*/true);
    codec::TraceBodyHeader th;
    th.kind = 0;
    th.trigger_event_id = trigger_event_id;
    th.trigger_tick = trigger_tick;
    th.payload_len = static_cast<std::uint32_t>(staging_len);
    hn = static_cast<std::uint16_t>(
        hn + codec::encode_trace_header(head + hn, static_cast<std::uint16_t>(sizeof(head) - hn), th));

    // Total stream = head (25) + staging (<= 489). Fragment chunk <= 114.
    const std::uint16_t total = static_cast<std::uint16_t>(hn + staging_len);
    constexpr std::uint16_t kChunk = codec::MaxPayload - 2;
    std::uint16_t frag_count = static_cast<std::uint16_t>((total + kChunk - 1) / kChunk);
    if (frag_count == 0)
    {
        frag_count = 1;
    }
    std::uint16_t offset = 0;
    for (std::uint16_t f = 0; f < frag_count; ++f)
    {
        std::uint8_t payload[codec::MaxPayload];
        payload[0] = static_cast<std::uint8_t>(f);
        payload[1] = static_cast<std::uint8_t>(frag_count);
        std::uint16_t m = 2;
        const std::uint16_t remaining = static_cast<std::uint16_t>(total - offset);
        const std::uint16_t take = remaining < kChunk ? remaining : kChunk;
        for (std::uint16_t i = 0; i < take; ++i)
        {
            const std::uint16_t stream_pos = static_cast<std::uint16_t>(offset + i);
            payload[m++] = stream_pos < hn ? head[stream_pos] : staging[stream_pos - hn];
        }
        offset = static_cast<std::uint16_t>(offset + take);

        codec::Header h;
        h.msg_family = static_cast<std::uint8_t>(codec::Family::Observability);
        h.msg_type = static_cast<std::uint8_t>(codec::MsgObservability::TraceRecord);
        h.queue_class = static_cast<std::uint8_t>(codec::QueueClass::Traces);
        h.flags = codec::FlagReserve; // fault-correlated trace (#47 section 8.2)
        std::uint8_t frame[codec::Mtu];
        const std::uint16_t flen = codec::encode(frame, sizeof(frame), h, payload, m);
        if (flen > 0)
        {
            (void)m_sink->enqueue(codec::QueueClass::Traces, frame, flen);
        }
    }
}

void Producer::answer_query(std::uint8_t sections_mask)
{
    if (!m_initialized)
    {
        return;
    }
    std::uint8_t doc[456];
    std::uint16_t doc_len = 0;
    assemble_snapshot(sections_mask, doc, doc_len);
    send_snapshot_fragments(doc, doc_len, codec::QueueClass::Control);
}

void Producer::push_birth(std::uint16_t authority_id)
{
    if (!m_initialized)
    {
        return;
    }
    std::uint8_t doc[456];
    std::uint16_t doc_len = 0;
    assemble_snapshot(0xFFu, doc, doc_len);
    send_snapshot_fragments(doc, doc_len, codec::QueueClass::Control);
    if (m_subs != nullptr)
    {
        m_subs->birth_sent(authority_id);
    }
}

void Producer::update_uptime(std::uint32_t now_ms)
{
    // sysUpTime (#49 section 6): seconds since boot, advanced once per second
    // edge, never per tick. Increments on edge TRANSITIONS (s != last), not by
    // copying s: stays monotonic across a 2^32 ms wrap of the monotonic clock
    // (~49.7 days, review NIT).
    const std::uint32_t s = now_ms / 1000u;
    if (s != m_last_uptime_s)
    {
        ++m_counters.uptime_s;
        m_last_uptime_s = s;
    }
}

std::uint8_t Producer::next_seq(codec::QueueClass cls)
{
    switch (cls)
    {
    case codec::QueueClass::Telemetry:
        return m_seq_telemetry++;
    case codec::QueueClass::Events:
        return m_seq_events++;
    case codec::QueueClass::Logs:
        return m_seq_logs++;
    case codec::QueueClass::Traces:
        return m_seq_traces++;
    default:
        return 0;
    }
}

std::uint8_t Producer::record_type(codec::QueueClass cls) const
{
    switch (cls)
    {
    case codec::QueueClass::Telemetry:
        return static_cast<std::uint8_t>(codec::MsgObservability::TelemetryRecord);
    case codec::QueueClass::Events:
        return static_cast<std::uint8_t>(codec::MsgObservability::EventRecord);
    case codec::QueueClass::Logs:
        return static_cast<std::uint8_t>(codec::MsgObservability::LogRecord);
    case codec::QueueClass::Traces:
        return static_cast<std::uint8_t>(codec::MsgObservability::TraceRecord);
    default:
        return 0;
    }
}

std::uint16_t Producer::fault_mask_from_diag() const
{
    // Wire codes #47 section 16.4, additive domain->bit mapping (design 2.5).
    std::uint16_t m = 0;
    if (m_diag == nullptr)
    {
        return m;
    }
    switch (static_cast<safety::SafetyFault>(m_diag->fault))
    {
    case safety::SafetyFault::DegradedTimeout:
        m |= 0x0001;
        break;
    case safety::SafetyFault::DirectionalToF:
        m |= 0x0002;
        break;
    case safety::SafetyFault::CanFailsafe:
        m |= 0x0004;
        break;
    case safety::SafetyFault::CrashMarker:
        m |= 0x0008;
        break;
    default:
        break;
    }
    return m;
}

std::uint16_t Producer::warning_mask_from_diag() const
{
    std::uint16_t m = 0;
    if (m_diag == nullptr)
    {
        return m;
    }
    switch (static_cast<safety::DegradedClass>(m_diag->degraded_class))
    {
    case safety::DegradedClass::Sensing:
        m |= 0x0001;
        break;
    case safety::DegradedClass::CanBus:
        m |= 0x0002;
        break;
    case safety::DegradedClass::Overtemp:
        m |= 0x0004;
        break;
    case safety::DegradedClass::BmsStale:
        m |= 0x0008;
        break;
    default:
        break;
    }
    return m;
}

std::uint8_t Producer::op_state_from_diag() const
{
    // Current intent kind as op_state (design 2.2: current operation/state).
    if (m_diag == nullptr)
    {
        return 0;
    }
    return static_cast<std::uint8_t>(m_diag->current_intent.kind);
}

void Producer::put16_drop(std::uint8_t* out, std::uint16_t& n, std::uint16_t a, std::uint16_t b) const
{
    if (n + 4 > 456u)
    {
        return;
    }
    out[n++] = static_cast<std::uint8_t>(a & 0xFFu);
    out[n++] = static_cast<std::uint8_t>((a >> 8) & 0xFFu);
    out[n++] = static_cast<std::uint8_t>(b & 0xFFu);
    out[n++] = static_cast<std::uint8_t>((b >> 8) & 0xFFu);
}

// --- Sink -------------------------------------------------------------------

bool Sink::is_query_frame(const QueueEntry& e)
{
    // Canonical frame: sync 0xE3 0x10 + header; Control family, Query msg_type
    // (semantic::handle_query forwards Query into the outbound Control path).
    if (e.len < 12u)
    {
        return false;
    }
    if (e.data[0] != codec::Sync0 || e.data[1] != codec::Sync1)
    {
        return false;
    }
    return e.data[3] == static_cast<std::uint8_t>(codec::Family::Control) &&
           e.data[4] == static_cast<std::uint8_t>(codec::MsgControl::Query);
}

void Sink::handle_query_frame(const QueueEntry& e)
{
    if (m_producer == nullptr)
    {
        return;
    }
    // Decode the Query sections_mask (payload starts after sync 2 + header 8).
    codec::Query q;
    const std::uint16_t payload_len = e.len >= 12u
                                          ? static_cast<std::uint16_t>(e.len - 12u)
                                          : 0u;
    if (codec::decode_query(e.data + 10, payload_len, q) != codec::CodecResult::Ok)
    {
        return;
    }
    m_producer->answer_query(q.sections_mask); // fragments enqueue to Control (prio 1)
}

void Sink::init(UartPort* uart, subscription::Registry* subs, Producer* producer)
{
    m_uart = uart;
    m_subs = subs;
    m_producer = producer;
}

bool Sink::push_class(codec::QueueClass cls, const std::uint8_t* data, std::uint32_t len)
{
    if (m_producer == nullptr)
    {
        return false;
    }
    // Bound to MTU (callers build canonical frames).
    if (len > codec::Mtu)
    {
        return false;
    }
    QueueEntry e;
    for (std::uint32_t i = 0; i < len; ++i)
    {
        e.data[i] = data[i];
    }
    e.len = static_cast<std::uint16_t>(len);

    auto push = [&](auto& q, std::uint32_t cap) {
        if (!q.full())
        {
            q.push(e);
            m_producer->note_high_water(cls, static_cast<std::uint32_t>(q.size()));
            return true;
        }
        return false;
    };

    switch (cls)
    {
    case codec::QueueClass::Control:
        if (!push(m_control, ControlCapacity))
        {
            m_producer->note_class_drop(cls); // answer queue full: dropped + event
            return false;
        }
        return true;
    case codec::QueueClass::Service:
        if (!push(m_service, ServiceCapacity))
        {
            m_producer->note_class_drop(cls);
            return false;
        }
        return true;
    case codec::QueueClass::Telemetry:
        if (!push(m_telemetry, TelemetryCapacity))
        {
            // drop-oldest (freshness): evict head, keep the NEW record.
            QueueEntry evicted;
            (void)m_telemetry.pop(evicted);
            (void)m_telemetry.push(e);
            m_producer->note_class_drop(cls);
        }
        return true; // accepted (freshness policy)
    case codec::QueueClass::Events:
        if (push(m_events, EventsCapacity))
        {
            append_tail_events(data, len); // tail mirror for fault capture
            return true;
        }
        m_producer->note_class_drop(cls); // drop-newest: reject the new record
        return false;
    case codec::QueueClass::Logs:
        if (push(m_logs, LogsCapacity))
        {
            append_tail_logs(data, len);
            return true;
        }
        m_producer->note_class_drop(cls);
        return false;
    case codec::QueueClass::Traces:
        if (!push(m_traces, TracesCapacity))
        {
            QueueEntry evicted;
            (void)m_traces.pop(evicted);
            (void)m_traces.push(e);
            m_producer->note_class_drop(cls);
        }
        return true;
    default:
        return false;
    }
}

bool Sink::enqueue(codec::QueueClass cls, const std::uint8_t* data, std::uint32_t len)
{
    return push_class(cls, data, len); // never blocks (#43 section 6)
}

void Sink::tick()
{
    if (m_uart == nullptr || m_producer == nullptr)
    {
        return;
    }
    std::uint32_t budget = LinkBudgetBytes;
    std::uint32_t spent_control = 0;
    std::uint32_t spent_events = 0;
    std::uint32_t spent_logs = 0;
    std::uint32_t spent_traces = 0;
    std::uint32_t spent_telemetry = 0;

    // Defer-on-backpressure drain (#49 section 10, #43 section 6): a frame
    // that does not fit the per-class per-tick cap, the total link budget, or
    // the UART ring stays QUEUED (head preserved) and drains on a later tick.
    // No drops happen here - drop policies apply only at enqueue (queue
    // overflow, class policy + counter). Never blocks (obligation #12).
    auto drain = [&](auto& q, std::uint32_t& spent, codec::QueueClass cls, std::uint32_t cap) {
        while (budget > 0 && !q.empty())
        {
            QueueEntry e;
            if (!q.peek(e))
            {
                break;
            }
            // Query interception (design §3.2): a Control-family Query frame is
            // NOT a wire response - the Sink routes it to the Producer, which
            // assembles the snapshot document and answers with fragments
            // (priority 1). The Query frame itself never reaches the UART.
            if (cls == codec::QueueClass::Control && is_query_frame(e))
            {
                (void)q.pop(e);
                handle_query_frame(e); // -> Producer::answer_query (bounded)
                continue;              // fragments land in the Control queue
            }
            const std::uint32_t need = static_cast<std::uint32_t>(e.len);
            if (spent + need > cap)
            {
                break; // class cap exhausted: head stays queued for the next tick
            }
            if (need > budget)
            {
                break; // link budget exhausted: defer
            }
            if (need > m_uart->tx_bytes_available())
            {
                break; // UART ring full: defer, never block
            }
            (void)q.pop(e);
            (void)m_uart->tx(e.data, need);
            budget -= need;
            spent += need;
        }
    };

    // Priority 1: Control/Service answers.
    drain(m_control, spent_control, codec::QueueClass::Control, PerClassCapBytes);
    drain(m_service, spent_control, codec::QueueClass::Service, PerClassCapBytes);
    // Priority 2: events.
    drain(m_events, spent_events, codec::QueueClass::Events, PerClassCapBytes);
    // Priority 3: logs.
    drain(m_logs, spent_logs, codec::QueueClass::Logs, PerClassCapBytes);
    // Priority 4: traces.
    drain(m_traces, spent_traces, codec::QueueClass::Traces, PerClassCapBytes);
    // Capture delivery completion (design §3.3): once the Traces queue is
    // fully drained after a fault capture, the Producer clears its pending
    // latch - a second fault before this point supersedes the capture
    // (enqueue is not delivery; the capture spans several ticks).
    if (m_capture_pending_flag && m_traces.empty())
    {
        m_capture_pending_flag = false;
        m_producer->on_capture_delivered();
    }
    // Priority 5: telemetry.
    drain(m_telemetry, spent_telemetry, codec::QueueClass::Telemetry, PerClassCapBytes);

    // Emit the coalesced drop event once the Events queue has capacity.
    m_producer->flush_pending_drop();
}

void Sink::append_tail_events(const std::uint8_t* data, std::uint32_t len)
{
    // Compact mirror (design 2.2): the RECORD = envelope (14) + EventBody (12)
    // = 26 B. `data` points at the canonical frame start (sync + header 8);
    // the record payload begins at data+10. Copy the payload, not the frame
    // prefix (review F4): the tail must be reassemblable into trace records.
    const std::uint32_t payload_start = len >= 10u ? 10u : len;
    const std::uint32_t payload_len = len - payload_start;
    const std::uint32_t copy = payload_len < 26u ? payload_len : 26u;
    const std::uint32_t idx = (m_tail_events_count % TailDepth) * 26u;
    for (std::uint32_t i = 0; i < copy; ++i)
    {
        m_tail_events[idx + i] = data[payload_start + i];
    }
    ++m_tail_events_count;
}

void Sink::append_tail_logs(const std::uint8_t* data, std::uint32_t len)
{
    // Compact mirror (design 2.2): envelope 14 + level/module 2 + text <= 16 =
    // <= 32 B. Payload begins at data+10 (after sync + header).
    const std::uint32_t payload_start = len >= 10u ? 10u : len;
    const std::uint32_t payload_len = len - payload_start;
    const std::uint32_t copy = payload_len < 32u ? payload_len : 32u;
    const std::uint32_t idx = (m_tail_logs_count % TailDepth) * 32u;
    for (std::uint32_t i = 0; i < copy; ++i)
    {
        m_tail_logs[idx + i] = data[payload_start + i];
    }
    ++m_tail_logs_count;
}

std::uint32_t Sink::tail_events(std::uint8_t* out, std::uint32_t cap) const
{
    const std::uint32_t total = m_tail_events_count < TailDepth ? m_tail_events_count : TailDepth;
    const std::uint32_t want = total * 26u;
    const std::uint32_t n = want < cap ? want : cap;
    const std::uint32_t skip_whole = n / 26u; // full records we can copy
    const std::uint32_t start =
        m_tail_events_count > TailDepth ? (m_tail_events_count - TailDepth) % TailDepth : 0u;
    std::uint32_t written = 0;
    for (std::uint32_t i = 0; i < skip_whole; ++i)
    {
        const std::uint32_t src = ((start + i) % TailDepth) * 26u;
        for (std::uint32_t b = 0; b < 26u; ++b)
        {
            out[written++] = m_tail_events[src + b];
        }
    }
    return written; // partial record tail: caller tolerates truncated tail
}

std::uint32_t Sink::tail_logs(std::uint8_t* out, std::uint32_t cap) const
{
    const std::uint32_t total = m_tail_logs_count < TailDepth ? m_tail_logs_count : TailDepth;
    const std::uint32_t want = total * 32u;
    const std::uint32_t n = want < cap ? want : cap;
    const std::uint32_t skip_whole = n / 32u;
    const std::uint32_t start =
        m_tail_logs_count > TailDepth ? (m_tail_logs_count - TailDepth) % TailDepth : 0u;
    std::uint32_t written = 0;
    for (std::uint32_t i = 0; i < skip_whole; ++i)
    {
        const std::uint32_t src = ((start + i) % TailDepth) * 32u;
        for (std::uint32_t b = 0; b < 32u; ++b)
        {
            out[written++] = m_tail_logs[src + b];
        }
    }
    return written;
}

void log_emit(Producer& p, std::uint8_t level, std::uint8_t module_id,
              const char* text, std::uint32_t len)
{
    codec::LogBody b;
    b.level = level;
    b.module_id = module_id;
    std::uint32_t n = len < 80u ? len : 80u;
    for (std::uint32_t i = 0; i < n; ++i)
    {
        b.text[i] = static_cast<std::uint8_t>(text[i]);
    }
    b.text_len = static_cast<std::uint16_t>(n);
    std::uint8_t body[codec::MaxPayload];
    const std::uint16_t blen = codec::encode_log_body(body, sizeof(body), b);
    p.emit_class(codec::QueueClass::Logs, body, blen, /*with_wall=*/true, 0, level);
}

} // namespace observability
} // namespace v3
