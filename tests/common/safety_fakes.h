// Host fakes for the Safety Authority slice (design
// docs/safety-authority-design-v3.md section 7.2; ticket #71). Deterministic:
// scripted sensing snapshots, CAN error-state/TX capture, persisted marker,
// recording safety events. Fixed-size buffers (no std::vector, R1 spirit).
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/safety_authority.h"

namespace testfakes
{

// Scripted sensing view: the Safety Authority reads snapshots; tests control
// freshness/fault state per sensor. Default: all sensors healthy + fresh.
class FakeSensingSnapshots : public v3::sensing::SensingView
{
  public:
    FakeSensingSnapshots()
    {
        // Default: all sensors have a fresh sample (age 0), state Healthy.
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v3::sensing::SensorId::Count); ++i)
        {
            m_snap[i].has_sample = true;
            m_snap[i].age_ms = 0u;
            m_snap[i].state = v3::sensing::HealthState::Healthy;
        }
    }

    void set_fresh_all(std::uint64_t age_ms)
    {
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(v3::sensing::SensorId::Count); ++i)
        {
            m_snap[i].has_sample = true;
            m_snap[i].age_ms = static_cast<std::uint32_t>(age_ms);
            m_snap[i].state = v3::sensing::HealthState::Healthy;
            m_snap[i].consecutive_failures = 0;
        }
    }
    void set_sensor(v3::sensing::SensorId id, v3::sensing::HealthState state, bool has_sample,
                    std::uint64_t age_ms)
    {
        v3::sensing::SensorSnapshot& s = m_snap[static_cast<std::uint32_t>(id)];
        s.has_sample = has_sample;
        s.age_ms = static_cast<std::uint32_t>(age_ms);
        s.state = state;
    }
    void set_faulted(v3::sensing::SensorId id)
    {
        set_sensor(id, v3::sensing::HealthState::Faulted, true, 0u);
    }

    bool get_snapshot(v3::sensing::SensorId id, v3::sensing::SensorSnapshot* out) const override
    {
        if (id >= v3::sensing::SensorId::Count)
        {
            return false;
        }
        *out = m_snap[static_cast<std::uint32_t>(id)];
        return true;
    }

  private:
    v3::sensing::SensorSnapshot m_snap[static_cast<std::uint32_t>(v3::sensing::SensorId::Count)] = {};
};

// CAN adapter fake: TX capture + per-tick budget (returns false when exhausted),
// scripted error-state, force-stop / recover counters, RX queue with overflow drop.
class FakeCanPort : public v3::CanPort
{
  public:
    static constexpr std::uint32_t kMaxTx = 64;
    static constexpr std::uint32_t kMaxRx = 128;

    bool tx(const v3::CanFrame& f) override
    {
        if (m_tx_count >= m_tx_budget)
        {
            ++m_dropped;
            return false;
        }
        if (m_tx_count < kMaxTx)
        {
            m_tx_frames[m_tx_count] = f;
        }
        ++m_tx_count;
        return true;
    }
    std::uint32_t rx_drain(v3::CanFrame* out, std::uint32_t budget) override
    {
        std::uint32_t n = 0;
        while (n < budget && n < m_rx_count)
        {
            out[n] = m_rx_queue[n];
            ++n;
        }
        if (m_rx_count > budget)
        {
            m_rx_dropped += m_rx_count - budget; // переполнение: drop + счётчик (#43 §4)
        }
        m_rx_count = 0;
        return n;
    }
    void force_stop_tx() override { ++m_force_stop_count; }
    v3::CanErrorState error_state() const override { return m_error_state; }
    void recover_bus_off() override { ++m_recover_count; }

    void set_error_state(v3::CanErrorState s) { m_error_state = s; }
    void set_tx_budget(std::uint32_t b) { m_tx_budget = b; }
    void push_rx(const v3::CanFrame& f)
    {
        if (m_rx_count < kMaxRx)
        {
            m_rx_queue[m_rx_count++] = f;
        }
    }

    std::uint32_t tx_count() const { return m_tx_count; }
    std::uint32_t dropped() const { return m_dropped; }
    std::uint32_t force_stop_count() const { return m_force_stop_count; }
    std::uint32_t recover_count() const { return m_recover_count; }
    std::uint32_t rx_dropped() const { return m_rx_dropped; }
    const v3::CanFrame& tx_frame(std::uint32_t i) const { return m_tx_frames[i]; }

  private:
    v3::CanFrame m_tx_frames[kMaxTx] = {};
    v3::CanFrame m_rx_queue[kMaxRx] = {};
    std::uint32_t m_tx_count = 0;
    std::uint32_t m_tx_budget = 16u; // #48 §7
    std::uint32_t m_dropped = 0;
    std::uint32_t m_rx_count = 0;
    std::uint32_t m_rx_dropped = 0;
    std::uint32_t m_force_stop_count = 0;
    std::uint32_t m_recover_count = 0;
    v3::CanErrorState m_error_state = v3::CanErrorState::Active;
};

// Persisted marker fake (Q5 A): read_crash does NOT clear (power-cycle != ack);
// clear_crash only via explicit ack path.
class FakeMarker : public v3::SafetyStateMarker
{
  public:
    void set_pending(std::uint32_t crash_count)
    {
        m_state.crash_pending = true;
        m_state.crash_count = crash_count;
    }

    void write_crash(std::uint32_t crash_count) override
    {
        m_state.crash_pending = true;
        m_state.crash_count = crash_count;
        ++m_write_count;
    }
    State read_crash() override
    {
        ++m_read_count;
        return m_state; // read-only: не снимает маркер
    }
    void clear_crash() override
    {
        m_state = State{};
        ++m_clear_count;
    }

    State state() const { return m_state; }
    std::uint32_t read_count() const { return m_read_count; }
    std::uint32_t clear_count() const { return m_clear_count; }
    std::uint32_t write_count() const { return m_write_count; }

  private:
    State m_state{};
    std::uint32_t m_read_count = 0;
    std::uint32_t m_clear_count = 0;
    std::uint32_t m_write_count = 0;
};

// Recording safety-event sink.
class FakeSafetyEvents : public v3::safety::SafetyAuthority::Events
{
  public:
    struct HealthChange
    {
        v3::safety::SafetyHealth from = v3::safety::SafetyHealth::Initializing;
        v3::safety::SafetyHealth to = v3::safety::SafetyHealth::Initializing;
        v3::safety::DegradedClass cls = v3::safety::DegradedClass::None;
        v3::safety::SafetyFault fault = v3::safety::SafetyFault::None;
    };

    void health_changed(v3::safety::SafetyHealth from, v3::safety::SafetyHealth to,
                        v3::safety::DegradedClass cls, v3::safety::SafetyFault fault) override
    {
        if (m_health_count < kMax)
        {
            m_health[m_health_count++] = HealthChange{from, to, cls, fault};
        }
    }
    void stop_issued(v3::safety::StopProfile profile, std::uint32_t seq) override
    {
        ++m_stop_count;
        m_last_profile = profile;
        m_last_seq = seq;
    }
    void can_failsafe(v3::CanErrorState state) override
    {
        ++m_can_failsafe_count;
        m_last_can_state = state;
    }
    void crash_marker_pending(std::uint32_t crash_count) override
    {
        ++m_marker_count;
        m_marker_crash = crash_count;
    }

    static constexpr std::uint32_t kMax = 32;

    std::uint32_t health_count() const { return m_health_count; }
    const HealthChange& health(std::uint32_t i) const { return m_health[i]; }
    std::uint32_t stop_count() const { return m_stop_count; }
    v3::safety::StopProfile last_profile() const { return m_last_profile; }
    std::uint32_t last_seq() const { return m_last_seq; }
    std::uint32_t can_failsafe_count() const { return m_can_failsafe_count; }
    v3::CanErrorState last_can_state() const { return m_last_can_state; }
    std::uint32_t marker_count() const { return m_marker_count; }
    std::uint32_t marker_crash() const { return m_marker_crash; }

  private:
    HealthChange m_health[kMax] = {};
    std::uint32_t m_health_count = 0;
    std::uint32_t m_stop_count = 0;
    v3::safety::StopProfile m_last_profile = v3::safety::StopProfile::Controlled;
    std::uint32_t m_last_seq = 0;
    std::uint32_t m_can_failsafe_count = 0;
    v3::CanErrorState m_last_can_state = v3::CanErrorState::Active;
    std::uint32_t m_marker_count = 0;
    std::uint32_t m_marker_crash = 0;
};

} // namespace testfakes
