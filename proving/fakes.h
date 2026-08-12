// Host fake adapters implementing domain/ports.h for the native test leg.
// Target builds use real adapters (adapters/, excluded from the native
// src_filter). Fakes are deterministic and record observables for tests.
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/queues.h"

namespace slice
{
namespace proving
{

class FakeCanPort : public CanPort
{
  public:
    static constexpr std::uint32_t kLogCapacity = 256;

    bool tx(const Frame& frame) override
    {
        if (m_tx_count >= kLogCapacity)
        {
            ++m_tx_dropped;
            return false;
        }
        m_tx_log[m_tx_count++] = frame;
        return true;
    }

    std::uint32_t rx_drain(Frame* out, std::uint32_t budget) override
    {
        std::uint32_t drained = 0;
        while (drained < budget && m_rx_count > 0)
        {
            out[drained++] = m_rx[m_rx_head++ % kLogCapacity];
            --m_rx_count;
        }
        return drained;
    }

    void force_stop_tx() override { ++m_force_stop_count; }

    void inject_rx(const Frame& frame)
    {
        if (m_rx_count >= kLogCapacity)
        {
            ++m_rx_overflow;
            return;
        }
        m_rx[(m_rx_head + m_rx_count) % kLogCapacity] = frame;
        ++m_rx_count;
    }

    // Observables.
    std::uint32_t tx_count() const { return m_tx_count; }
    std::uint32_t force_stop_count() const { return m_force_stop_count; }
    std::uint32_t rx_overflow() const { return m_rx_overflow; }
    std::uint32_t tx_dropped() const { return m_tx_dropped; }

  private:
    Frame m_tx_log[kLogCapacity] = {};
    std::uint32_t m_tx_count = 0;
    std::uint32_t m_tx_dropped = 0;
    Frame m_rx[kLogCapacity] = {};
    std::uint32_t m_rx_count = 0;
    std::uint32_t m_rx_head = 0;
    std::uint32_t m_rx_overflow = 0;
    std::uint32_t m_force_stop_count = 0;
};

class FakeObservabilitySink : public ObservabilityPort
{
  public:
    bool emit(Class cls, const std::uint8_t* data, std::uint32_t len) override
    {
        (void)data;
        (void)len;
        switch (cls)
        {
            case Class::Telemetry:
                m_queues.telemetry_push(0);
                return m_queues.dropped_telemetry() == 0;
            case Class::Events:
                m_queues.events_push(0);
                return m_queues.dropped_events() == 0;
            case Class::Logs:
                m_queues.logs_push(0);
                return m_queues.dropped_logs() == 0;
            case Class::Traces:
                m_queues.traces_push(0);
                return m_queues.dropped_traces() == 0;
        }
        return false;
    }

    std::uint32_t dropped(Class cls) const override
    {
        switch (cls)
        {
            case Class::Telemetry:
                return m_queues.dropped_telemetry();
            case Class::Events:
                return m_queues.dropped_events();
            case Class::Logs:
                return m_queues.dropped_logs();
            case Class::Traces:
                return m_queues.dropped_traces();
        }
        return 0;
    }

    QueueClasses& queues() { return m_queues; }

  private:
    QueueClasses m_queues;
};

// Minimal fake flash with simulated durations (host leg of obligation #3: the
// target measurement replaces these numbers with real PCB timing).
class FakeFlashPort : public FlashPort
{
  public:
    explicit FakeFlashPort(std::uint32_t erase_ms, std::uint32_t program_ms)
        : m_erase_ms(erase_ms), m_program_ms(program_ms)
    {
    }

    std::uint32_t erase_sector() override { return m_erase_ms; }
    std::uint32_t program_page(const std::uint8_t*) override { return m_program_ms; }

  private:
    std::uint32_t m_erase_ms;
    std::uint32_t m_program_ms;
};

} // namespace proving
} // namespace slice
