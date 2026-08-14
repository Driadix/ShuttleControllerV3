// Subscription control plane (design docs/operation-runtime-design-v3.md
// section 2.7, 3.5; #49 section 9; ticket #74). Bounded agreements
// (class(es), filter, minInterval, maxBytesPerTick) per principal; caps
// bridge 8 / radio 2 (#49 section 9); dies with the session/epoch (#49,
// #46 I-LC-6). Delivery enforcement (per-tick byte caps, slow-consumer
// drops on the sink side) belongs to the Observability Sink (#72) - this
// registry provides the STATE and the interest signal.
#pragma once

#include <cstdint>

#include "domain/codec.h"
#include "domain/ports.h"

namespace v3
{
namespace subscription
{

struct Subscription
{
    std::uint8_t sub_id = 0;
    std::uint16_t authority_id = 0;
    std::uint8_t class_mask = 0;
    std::uint8_t filter = 0;
    std::uint16_t min_interval_ms = 0;
    std::uint16_t max_bytes_per_tick = 0;
    bool active = false;
};

class Registry
{
  public:
    static constexpr std::uint8_t BridgeCap = 8; // #49 section 9
    static constexpr std::uint8_t RadioCap = 2;

    enum class Result : std::uint8_t
    {
        Ok = 0,
        CapsExceeded = 1,
        UnknownSub = 2,
    };

    // profile: 0 = network_bridge (default), 1 = radio (effective profile is
    // controller-derived from ingress, #47 section 5.1; #75 supplies it).
    void init(std::uint8_t profile, RuntimeEvents* events);

    Result subscribe(std::uint16_t authority_id, const codec::Subscribe& s, std::uint8_t& sub_id);
    Result unsubscribe(std::uint16_t authority_id, std::uint8_t sub_id);
    void epoch_reset(); // subscriptions die with the session/epoch (#49 section 9)

    // Interest: a class stream exists with >= 1 active subscription covering
    // it, or a declared profile default. Defaults (#49 section 9): bridge -
    // telemetry + events always; radio - events always, telemetry only by
    // subscription; logs/traces - push only by subscription.
    bool interest(codec::QueueClass cls) const;

    bool birth_pending(std::uint16_t authority_id) const; // birth push (#49 section 2.6)
    void birth_sent(std::uint16_t authority_id);

    // Slow-consumer drop (per-subscription counter is observed via event;
    // the counter itself lives at the Producer, #43 section 4).
    void note_drop(std::uint8_t sub_id);
    std::uint32_t drops() const { return m_drops; }
    std::uint8_t active_count() const;

  private:
    static constexpr std::uint8_t ClassBitTelemetry = 0x01;
    static constexpr std::uint8_t ClassBitEvents = 0x02;
    static constexpr std::uint8_t ClassBitLogs = 0x04;
    static constexpr std::uint8_t ClassBitTraces = 0x08;

    std::uint8_t cap() const { return m_profile == 0 ? BridgeCap : RadioCap; }
    bool has_class(const Subscription& s, codec::QueueClass cls) const;
    bool any_sub_class(codec::QueueClass cls) const;
    std::int32_t find_slot(std::uint16_t authority_id, std::uint8_t sub_id) const;
    std::int32_t find_dup(std::uint16_t authority_id, std::uint8_t class_mask) const;

    Subscription m_subs[BridgeCap]; // 8 slots (max cap)
    bool m_birth[BridgeCap] = {};
    std::uint8_t m_next_sub_id = 1; // 0 = invalid
    std::uint8_t m_profile = 0;
    std::uint32_t m_drops = 0;
    RuntimeEvents* m_events = nullptr;
};

} // namespace subscription
} // namespace v3
