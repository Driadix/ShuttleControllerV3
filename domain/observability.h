// Observability Producer + Sink-политика (design
// docs/observability-design-v3.md sections 2, 5.2; architecture #49; ticket #72).
// Framework-free, host-deterministic (rule R6, #51 section 5): depends only on
// domain ports and types - no Arduino, no adapters.
//
// Ownership (issue #43 section 4, #49 section 4):
//   - Producer: envelopes, class records, counters (SOLE owner), snapshot
//     assembly, fault-capture staging; implements KernelEvents, RuntimeEvents
//     and safety::SafetyAuthority::Events.
//   - Sink policy: class queues (8/32/32/16, #48 section 6) + outbound
//     Control 16 / Service 8, drop policies (#43 section 6), priorities and
//     per-tick caps (#49 section 10), Query/birth routing; implements
//     OutboundControl. HAL (USART1) lives in adapters/uart_bridge.
//
// ISR boundary (rule R2): the only ISR in scope #72 is the UART TXE handler
// in adapters/uart_bridge - it moves ring->TDR and nothing else. No policy,
// counters, events or emission from an ISR; all Sink/Producer work is
// foreground-only (bounded kernel steps).
#pragma once

#include <cstdint>

#include "domain/codec.h"
#include "domain/ports.h"
#include "domain/runtime.h"
#include "domain/safety_authority.h"
#include "domain/safety_intent.h"
#include "domain/safety_state.h"
#include "domain/sensing.h"
#include "domain/static_queue.h"
#include "domain/subscriptions.h"

namespace v3
{
namespace observability
{

// ---------------------------------------------------------------------------
// Counters (Producer owns them; #43 section 4, #49 section 6)
// ---------------------------------------------------------------------------
struct ProducerCounters
{
    std::uint32_t uptime_s = 0;
    std::uint32_t reset_by_category[5] = {}; // ResetCause histogram
    std::uint32_t last_boot_cause = 0;
    std::uint16_t drop_telemetry = 0;
    std::uint16_t drop_events = 0;
    std::uint16_t drop_logs = 0;
    std::uint16_t drop_traces = 0;
    std::uint16_t high_water_telemetry = 0;
    std::uint16_t high_water_events = 0;
    std::uint16_t high_water_logs = 0;
    std::uint16_t high_water_traces = 0;
    std::uint16_t trace_capture_superseded = 0;
    std::uint16_t admission_rejects[22] = {}; // per RejectCode (bounded)
    std::uint16_t subscription_drops = 0;
};

class Sink;

// ---------------------------------------------------------------------------
// Producer
// ---------------------------------------------------------------------------
class Producer : public KernelEvents, public RuntimeEvents,
                 public safety::SafetyAuthority::Events
{
  public:
    void init(EpochSource* epoch, WindowSource* window, HealthSource* health,
              ProvisioningSource* prov, subscription::Registry* subs,
              runtime::Runtime* rt, sensing::SensingView* sensing,
              SafetyDiag* diag, WallClockSource* wall, IdentitySource* identity,
              Sink* sink);

    // KernelEvents (foreground-only calls from the execution core).
    void step_overrun(std::uint32_t step_ms) override;
    void scheduler_gap(std::uint64_t gap_ms) override;
    void schedule_rejected() override;
    void reset_cause(ResetCause cause) override;

    // RuntimeEvents (#74 Semantic Contract / Runtime).
    void admission_rejected(std::uint8_t reject_code) override;
    void request_duplicate(std::uint32_t request_id, bool conflict) override;
    void transport_error(codec::TransportError e) override;
    void queue_rejected(codec::QueueClass cls) override;
    void operation_started(std::uint32_t op_id, std::uint16_t type_id) override;
    void operation_terminal(std::uint32_t op_id, std::uint16_t type_id,
                            std::uint16_t outcome_code) override;
    void subscription_changed(std::uint16_t authority_id, bool active) override;
    void subscription_drop(std::uint8_t sub_id) override;

    // safety::SafetyAuthority::Events (#71).
    void health_changed(safety::SafetyHealth from, safety::SafetyHealth to,
                        safety::DegradedClass cls, safety::SafetyFault fault) override;
    void stop_issued(safety::StopProfile profile, std::uint32_t seq) override;
    void can_failsafe(CanErrorState state) override;
    void crash_marker_pending(std::uint32_t crash_count) override;

    // Drop channel Sink->Producer (single-writer, #43 section 4): the Sink
    // reports, the counter AND the 0x05xx event live here. The event is a
    // COALESCED pending latch: when the Events queue is full, reporting a drop
    // must not synchronously enqueue another event (recursion/churn, #49
    // section 10 defer-on-backpressure). The Sink calls flush_pending_drop()
    // after its drain, once event capacity returns.
    void note_class_drop(codec::QueueClass cls);
    void flush_pending_drop();
    void note_high_water(codec::QueueClass cls, std::uint32_t size);

    // Telemetry: assembles the body from sources (health/diag/sensing/runtime),
    // emits only when interest(Telemetry) (bridge default: 300 ms cadence is
    // scheduled by the glue; #49 section 9). Foreground, bounded.
    void emit_telemetry();

    // Fault capture (crash-class only, #49 section 8.2): staging <= 512 B,
    // supersede on a second pending capture, delivery as traces fragments.
    // Triggers: reset_cause (startup), crash_marker_pending, health->Fault for
    // non-auto-clear classes (CanFailsafe, CrashMarker) only.
    void start_fault_capture(std::uint16_t trigger_event_id, std::uint32_t trigger_tick);

    // Query answer: document by sections_mask, <= 456 B, <= 4 fragments (bridge,
    // #49 section 10). Called by the Sink when it recognizes a Query frame.
    void answer_query(std::uint8_t sections_mask);

    // Birth: full document on (re)subscribe (in the subscription maxBytesPerTick).
    void push_birth(std::uint16_t authority_id);

    const ProducerCounters& counters() const { return m_counters; }
    // Boot-cause for snapshot identity (last observed reset cause).
    ResetCause boot_cause() const { return m_boot_cause; }
    // Document version (monotonic +1 on change; snapshot fencing, #49 section 2.6).
    std::uint32_t doc_version() const { return m_doc_version; }

    // Low-level emit (used by log_emit and tests): builds envelope+body into a
    // canonical frame and routes it to the Sink. monotonic_tick comes from the
    // last set_now() (glue calls it every tick); seq per class.
    void emit_class(codec::QueueClass cls, const std::uint8_t* body, std::uint16_t body_len,
                    bool with_wall, std::uint16_t event_id, std::uint8_t severity);

    // Monotonic tick from the glue (foreground, once per kernel tick): stamped
    // into envelopes; the glue must call this before emitting any record.
    void set_now(std::uint32_t now_ms) { m_now_ms = now_ms; }
    std::uint32_t now_ms() const { return m_now_ms; }

    // Uptime counter (sysUpTime, #49 section 6): advances once per second edge;
    // the glue calls it every tick with monotonic now.
    void update_uptime(std::uint32_t now_ms);
    std::uint32_t uptime_s() const { return m_counters.uptime_s; }

    // Called by the Sink when a fault capture's trace fragments have fully
    // drained (design §3.3): clears the pending latch so the next crash-class
    // fault starts a fresh capture instead of superseding.
    void on_capture_delivered();

  private:
    void emit_event(std::uint16_t event_id, std::uint8_t severity, std::uint8_t ctx_kind,
                    std::uint32_t ctx_value, std::uint32_t ctx_value2);
    void bump_counter(codec::QueueClass cls);
    void assemble_snapshot(std::uint8_t sections_mask, std::uint8_t* out, std::uint16_t& out_len);
    void send_snapshot_fragments(const std::uint8_t* doc, std::uint16_t doc_len,
                                 codec::QueueClass cls);
    void send_trace_capture(std::uint16_t trigger_event_id, std::uint32_t trigger_tick,
                            const std::uint8_t* staging, std::uint16_t staging_len);
    std::uint8_t next_seq(codec::QueueClass cls);
    std::uint8_t record_type(codec::QueueClass cls) const;
    std::uint16_t fault_mask_from_diag() const;
    std::uint16_t warning_mask_from_diag() const;
    std::uint8_t op_state_from_diag() const;
    void put16_drop(std::uint8_t* out, std::uint16_t& n, std::uint16_t a, std::uint16_t b) const;

    EpochSource* m_epoch = nullptr;
    WindowSource* m_window = nullptr;
    HealthSource* m_health = nullptr;
    ProvisioningSource* m_prov = nullptr;
    subscription::Registry* m_subs = nullptr;
    runtime::Runtime* m_rt = nullptr;
    sensing::SensingView* m_sensing = nullptr;
    SafetyDiag* m_diag = nullptr;
    WallClockSource* m_wall = nullptr;
    IdentitySource* m_identity = nullptr;
    Sink* m_sink = nullptr;

    ProducerCounters m_counters{};
    ResetCause m_boot_cause = ResetCause::PowerOn;
    bool m_boot_cause_set = false;
    std::uint32_t m_doc_version = 0;
    std::uint8_t m_seq_telemetry = 0;
    std::uint8_t m_seq_events = 0;
    std::uint8_t m_seq_logs = 0;
    std::uint8_t m_seq_traces = 0;
    bool m_in_emit = false; // re-entrancy guard: nested 0x05xx emission is counter-only
    bool m_initialized = false;
    std::uint32_t m_now_ms = 0; // monotonic tick from the glue (set_now)
    bool m_pending_drop = false;       // coalesced drop event latch (defer-on-backpressure)
    std::uint8_t m_pending_drop_cls = 0;
    std::uint32_t m_last_uptime_s = 0; // last uptime second edge (update_uptime)

    // Fault-capture staging (static 512 B, #48 section 8; not subject to class
    // drop policies, #49 section 2.5).
    std::uint8_t m_staging[512];
    std::uint16_t m_staging_len = 0;
    bool m_capture_pending = false;
};

// ---------------------------------------------------------------------------
// Sink policy
// ---------------------------------------------------------------------------
class Sink : public OutboundControl
{
  public:
    static constexpr std::uint32_t TelemetryCapacity = 8;  // drop-oldest (#48 section 6)
    static constexpr std::uint32_t EventsCapacity = 32;    // drop-newest
    static constexpr std::uint32_t LogsCapacity = 32;      // drop-newest
    static constexpr std::uint32_t TracesCapacity = 16;    // drop-oldest
    static constexpr std::uint32_t ControlCapacity = 16;   // outbound answers (design 3.4)
    static constexpr std::uint32_t ServiceCapacity = 8;
    static constexpr std::uint32_t PerClassCapBytes = 128; // bridge per-tick class cap (#49 section 10)
    static constexpr std::uint32_t LinkBudgetBytes = 230;  // bridge RX+TX; RX inactive in #72

    void init(UartPort* uart, subscription::Registry* subs, Producer* producer);

    // OutboundControl (#74): control answers (ACK/Query/sub ack) into the
    // priority-1 Control/Service queues. Never blocks (bounded; full -> false).
    bool enqueue(codec::QueueClass cls, const std::uint8_t* data, std::uint32_t len) override;

    // Drain by priority (1 Control/Service, 2 events, 3 logs, 4 traces,
    // 5 telemetry), per-class caps + total budget; never blocks (#43 section 6,
    // obligation #12). Foreground kernel step.
    void tick();

    // Queues peeked by Producer for fault-capture tail copy.
    std::uint32_t tail_events(std::uint8_t* out, std::uint32_t cap) const;
    std::uint32_t tail_logs(std::uint8_t* out, std::uint32_t cap) const;
    // Events-queue capacity probe for the coalesced drop-event flush (Producer).
    bool events_full() const { return m_events.full(); }
    // Called by the Producer after enqueueing a fault capture: the Sink tracks
    // the delivery window and reports drain completion (on_capture_delivered).
    void note_capture_pending() { m_capture_pending_flag = true; }
    // Fault-capture supersede (design §3.3): drop the still-queued fragments of
    // the superseded capture so they never interleave with the new capture.
    void drop_pending_capture() { m_traces.clear(); }

  private:
    struct QueueEntry
    {
        std::uint8_t data[codec::Mtu];
        std::uint16_t len = 0;
    };
    // Returns true when the record was accepted (or handled by class policy);
    // false only for Control/Service answer queues that are full (bounded
    // answers, OutboundControl contract).
    bool push_class(codec::QueueClass cls, const std::uint8_t* data, std::uint32_t len);
    void append_tail_events(const std::uint8_t* data, std::uint32_t len);
    void append_tail_logs(const std::uint8_t* data, std::uint32_t len);
    // Query interception (design §3.2): a Control-family Query frame is not a
    // wire response - the Sink routes it to Producer::answer_query (snapshot
    // fragments). Query frames never reach the UART.
    static bool is_query_frame(const QueueEntry& e);
    void handle_query_frame(const QueueEntry& e);

    UartPort* m_uart = nullptr;
    subscription::Registry* m_subs = nullptr;
    Producer* m_producer = nullptr;

    slice::StaticQueue<QueueEntry, ControlCapacity> m_control;
    slice::StaticQueue<QueueEntry, ServiceCapacity> m_service;
    slice::StaticQueue<QueueEntry, TelemetryCapacity> m_telemetry;
    slice::StaticQueue<QueueEntry, EventsCapacity> m_events;
    slice::StaticQueue<QueueEntry, LogsCapacity> m_logs;
    slice::StaticQueue<QueueEntry, TracesCapacity> m_traces;
    // True between a fault-capture enqueue and the Traces queue fully draining
    // (Sink::tick clears it and calls Producer::on_capture_delivered).
    bool m_capture_pending_flag = false;

    // Fault-capture tail mirrors (compact: events 26 B, logs 32 B; design 2.2).
    static constexpr std::uint32_t TailDepth = 8;
    std::uint8_t m_tail_events[TailDepth * 26] = {};
    std::uint8_t m_tail_logs[TailDepth * 32] = {};
    std::uint32_t m_tail_events_count = 0;
    std::uint32_t m_tail_logs_count = 0;
};

// Bounded log helper (design section 5.2): truncates text to <= 80 B without
// chunk split, emits a Logs record. Threshold filtering happens at call sites
// (compile-time DEBUG sites are excluded in production by V1_DEBUG pattern).
void log_emit(Producer& p, std::uint8_t level, std::uint8_t module_id,
              const char* text, std::uint32_t len);

} // namespace observability
} // namespace v3
