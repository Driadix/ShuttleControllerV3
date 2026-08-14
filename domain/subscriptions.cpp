// Subscription registry implementation (design
// docs/operation-runtime-design-v3.md section 3.5; #49 section 9; ticket #74).
#include "domain/subscriptions.h"

namespace v3
{
namespace subscription
{

void Registry::init(std::uint8_t profile, RuntimeEvents* events)
{
    m_profile = profile;
    m_events = events;
    epoch_reset();
}

std::int32_t Registry::find_slot(std::uint16_t authority_id, std::uint8_t sub_id) const
{
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active && m_subs[i].authority_id == authority_id && m_subs[i].sub_id == sub_id)
        {
            return i;
        }
    }
    return -1;
}

std::int32_t Registry::find_dup(std::uint16_t authority_id, std::uint8_t class_mask) const
{
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active && m_subs[i].authority_id == authority_id && m_subs[i].class_mask == class_mask)
        {
            return i;
        }
    }
    return -1;
}

bool Registry::has_class(const Subscription& s, codec::QueueClass cls) const
{
    switch (cls)
    {
    case codec::QueueClass::Telemetry:
        return (s.class_mask & ClassBitTelemetry) != 0u;
    case codec::QueueClass::Events:
        return (s.class_mask & ClassBitEvents) != 0u;
    case codec::QueueClass::Logs:
        return (s.class_mask & ClassBitLogs) != 0u;
    case codec::QueueClass::Traces:
        return (s.class_mask & ClassBitTraces) != 0u;
    default:
        return false;
    }
}

bool Registry::any_sub_class(codec::QueueClass cls) const
{
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active && has_class(m_subs[i], cls))
        {
            return true;
        }
    }
    return false;
}

Registry::Result Registry::subscribe(std::uint16_t authority_id, const codec::Subscribe& s,
                                     std::uint8_t& sub_id)
{
    // Duplicate active subscription (same principal, same class mask):
    // parameter update, idempotent.
    const std::int32_t dup = find_dup(authority_id, s.class_mask);
    if (dup >= 0)
    {
        Subscription& d = m_subs[static_cast<std::uint8_t>(dup)];
        d.filter = s.filter;
        d.min_interval_ms = s.min_interval_ms;
        d.max_bytes_per_tick = s.max_bytes_per_tick;
        sub_id = d.sub_id;
        m_birth[static_cast<std::uint8_t>(dup)] = true; // (re)subscribe: birth pattern (#49)
        if (m_events != nullptr)
        {
            m_events->subscription_changed(authority_id, true);
        }
        return Result::Ok;
    }

    std::uint8_t free_slot = BridgeCap;
    std::uint8_t used = 0;
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active)
        {
            ++used;
        }
        else if (free_slot == BridgeCap)
        {
            free_slot = i;
        }
    }
    if (free_slot == BridgeCap || used >= cap())
    {
        return Result::CapsExceeded;
    }

    Subscription& n = m_subs[free_slot];
    n.sub_id = m_next_sub_id;
    n.authority_id = authority_id;
    n.class_mask = s.class_mask;
    n.filter = s.filter;
    n.min_interval_ms = s.min_interval_ms;
    n.max_bytes_per_tick = s.max_bytes_per_tick;
    n.active = true;
    ++m_next_sub_id;
    if (m_next_sub_id == 0)
    {
        ++m_next_sub_id; // 0 = invalid
    }
    m_birth[free_slot] = true; // birth push on (re)subscribe (#49 section 2.6)
    sub_id = n.sub_id;
    if (m_events != nullptr)
    {
        m_events->subscription_changed(authority_id, true);
    }
    return Result::Ok;
}

Registry::Result Registry::unsubscribe(std::uint16_t authority_id, std::uint8_t sub_id)
{
    const std::int32_t idx = find_slot(authority_id, sub_id);
    if (idx < 0)
    {
        return Result::UnknownSub;
    }
    m_subs[static_cast<std::uint8_t>(idx)].active = false;
    m_birth[static_cast<std::uint8_t>(idx)] = false;
    if (m_events != nullptr)
    {
        m_events->subscription_changed(authority_id, false);
    }
    return Result::Ok;
}

void Registry::epoch_reset()
{
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        m_subs[i].active = false;
        m_birth[i] = false;
    }
    m_next_sub_id = 1;
}

bool Registry::interest(codec::QueueClass cls) const
{
    switch (cls)
    {
    case codec::QueueClass::Telemetry:
        // bridge default telemetry (300 ms, #49 section 9) or subscription.
        return m_profile == 0 || any_sub_class(cls);
    case codec::QueueClass::Events:
        // events always (reserve) on both profiles (#49 section 9, Q3).
        return true;
    case codec::QueueClass::Logs:
    case codec::QueueClass::Traces:
        return any_sub_class(cls); // push only by subscription (pull otherwise)
    default:
        return false;
    }
}

bool Registry::birth_pending(std::uint16_t authority_id) const
{
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active && m_subs[i].authority_id == authority_id && m_birth[i])
        {
            return true;
        }
    }
    return false;
}

void Registry::birth_sent(std::uint16_t authority_id)
{
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active && m_subs[i].authority_id == authority_id)
        {
            m_birth[i] = false;
        }
    }
}

void Registry::note_drop(std::uint8_t sub_id)
{
    ++m_drops;
    if (m_events != nullptr)
    {
        m_events->subscription_drop(sub_id);
    }
}

std::uint8_t Registry::active_count() const
{
    std::uint8_t used = 0;
    for (std::uint8_t i = 0; i < BridgeCap; ++i)
    {
        if (m_subs[i].active)
        {
            ++used;
        }
    }
    return used;
}

} // namespace subscription
} // namespace v3
