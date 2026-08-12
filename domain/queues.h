// Static queue classes (issue #43 section 6, budgets #48 section 6).
// Incoming: Control 18 (16 + 2 reserve), Service 8, Update 4.
// Outgoing: telemetry 8 (drop-oldest), events 32 / logs 32 (drop-newest),
// traces 16 (drop-oldest).
#pragma once

#include <cstdint>

#include "domain/static_queue.h"

namespace slice
{

using ControlFrame = std::uint16_t; // synthetic frame payload for the slice
using Byte = std::uint8_t;

class QueueClasses
{
  public:
    static constexpr std::uint32_t ControlCapacity = 18; // 16 + 2 reserve (stop/handshake never rejected)
    static constexpr std::uint32_t ServiceCapacity = 8;
    static constexpr std::uint32_t UpdateCapacity = 4; // reserve 2 while in-progress

    static constexpr std::uint32_t TelemetryCapacity = 8;
    static constexpr std::uint32_t EventsCapacity = 32;
    static constexpr std::uint32_t LogsCapacity = 32;
    static constexpr std::uint32_t TracesCapacity = 16;

    // Ingress. Admission policy (reject on admission, reserve for stop/handshake):
    // Control keeps 2 slots free for safety/control reserve (issue #43 section 6).
    bool control_push(const ControlFrame& frame) { return m_control.push(frame); }
    bool service_push(const ControlFrame& frame) { return m_service.push(frame); }
    bool update_push(const ControlFrame& frame) { return m_update.push(frame); }

    bool control_pop(ControlFrame& out) { return m_control.pop(out); }
    bool service_pop(ControlFrame& out) { return m_service.pop(out); }
    bool update_pop(ControlFrame& out) { return m_update.pop(out); }

    // Egress. Overload: telemetry/traces drop-oldest (freshness), events/logs
    // drop-newest (never destroy early fault evidence).
    void telemetry_push(Byte b) { push_drop_oldest(m_telemetry, b); }
    void events_push(Byte b) { push_drop_newest(m_events, b); }
    void logs_push(Byte b) { push_drop_newest(m_logs, b); }
    void traces_push(Byte b) { push_drop_oldest(m_traces, b); }

    // Observable counters (obligation #7: overflows observable).
    std::uint32_t dropped_telemetry() const { return m_telemetry.overflow_count(); }
    std::uint32_t dropped_events() const { return m_events.overflow_count(); }
    std::uint32_t dropped_logs() const { return m_logs.overflow_count(); }
    std::uint32_t dropped_traces() const { return m_traces.overflow_count(); }
    std::uint32_t rejected_control() const { return m_control.overflow_count(); }

    std::uint32_t control_size() const { return static_cast<std::uint32_t>(m_control.size()); }
    std::uint32_t telemetry_size() const { return static_cast<std::uint32_t>(m_telemetry.size()); }

  private:
    template <std::size_t N>
    static void push_drop_oldest(StaticQueue<Byte, N>& q, Byte b)
    {
        if (!q.push(b) && !q.empty())
        {
            Byte discard = 0;
            (void)q.pop(discard); // drop oldest, keep freshest
            (void)q.push(b);
        }
    }

    static void push_drop_newest(StaticQueue<Byte, 32>& q, Byte b)
    {
        (void)q.push(b); // full => drop-newest (push fails, overflow counted)
    }

    StaticQueue<ControlFrame, ControlCapacity> m_control;
    StaticQueue<ControlFrame, ServiceCapacity> m_service;
    StaticQueue<ControlFrame, UpdateCapacity> m_update;
    StaticQueue<Byte, TelemetryCapacity> m_telemetry;
    StaticQueue<Byte, EventsCapacity> m_events;
    StaticQueue<Byte, LogsCapacity> m_logs;
    StaticQueue<Byte, TracesCapacity> m_traces;
};

} // namespace slice
