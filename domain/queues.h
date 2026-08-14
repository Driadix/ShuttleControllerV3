// Production inbound queue classes (design docs/operation-runtime-design-v3.md
// section 2.8; #43 section 6, #48 section 6). Frame-based (MTU 128 B).
// Overload: Control/Service - reject on admission; Update - reject of new
// transactions (in-progress reserve is update-flow policy, #76). Reserve
// semantics: stop/handshake frames (codec::FlagReserve) land in dedicated
// reserve slots and are NEVER rejected by a full working queue (#43 section 6,
// #45 section 4: stop intents never rejected).
//
// Outbound classes (telemetry/events/logs/traces) are NOT here - they belong
// to the Observability Sink (#72).
#pragma once

#include <cstdint>

#include "domain/static_queue.h"

namespace v3
{
namespace queue
{

struct Frame
{
    std::uint8_t data[128] = {};
    std::uint16_t len = 0;
};

enum class Class : std::uint8_t
{
    Control = 0,
    Service = 1,
    Update = 2,
};

class InboundQueue
{
  public:
    static constexpr std::uint32_t ControlCapacity = 16; // рабочие слота
    static constexpr std::uint32_t ControlReserve = 2;   // резерв stop/handshake (#48 §6: 16+2)
    static constexpr std::uint32_t ServiceCapacity = 8;
    static constexpr std::uint32_t UpdateCapacity = 4;

    // reserve=true (stop/handshake): dedicated reserve slot, never rejected by
    // a full working queue. reserve=false + full: rejected (observable counter).
    // Never blocks (#43 section 6).
    bool push(Class cls, const Frame& f, bool reserve);

    // Popping prefers reserve slots first (stop/handshake drain with priority).
    bool pop(Class cls, Frame& out);

    bool is_full(Class cls) const;
    std::uint32_t size(Class cls) const;
    std::uint32_t rejected(Class cls) const; // reject-on-admission counter (obs #7)

  private:
    slice::StaticQueue<Frame, ControlCapacity> m_control;
    slice::StaticQueue<Frame, ControlReserve> m_control_reserve;
    slice::StaticQueue<Frame, ServiceCapacity> m_service;
    slice::StaticQueue<Frame, UpdateCapacity> m_update;
    std::uint32_t m_rejected_control = 0;
    std::uint32_t m_rejected_service = 0;
    std::uint32_t m_rejected_update = 0;
};

} // namespace queue
} // namespace v3
