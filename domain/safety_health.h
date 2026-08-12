// Simplified Safety Authority health model for the proving slice
// (safety model #45 section 2, budgets #48 section 2).
// Slice-grade: freshness (T_fresh), Degraded timeout (T_deg), Fault latch.
// This is NOT the production Safety Authority design (issue #43) - it exists
// to make execution properties measurable in the harness.
#pragma once

#include <cstdint>

namespace slice
{

enum class Health : std::uint8_t
{
    Initializing = 0, // startup, motion inhibited (INV-STARTUP-GATE)
    Ready = 1,        // motion allowed
    Degraded = 2,     // motion restricted by capability class
    Fault = 3,        // latched fault, motion inhibited
};

class SafetyHealth
{
  public:
    static constexpr std::uint64_t T_fresh_ms = 300;  // ToF directional freshness (issue #45 O3)
    static constexpr std::uint64_t T_deg_ms = 60'000; // continuous Degraded => Fault (#48 Q2)
    static constexpr std::uint64_t T_sample_worst_budget_ms = 200; // C1a: T_sample_worst + margin < 300 (#48)

    // Called once per bounded step. `now` is the monotonic time in ms.
    // `sample_age_ms` is the age of the freshest directional sensor sample.
    void tick(std::uint64_t now, std::uint64_t sample_age_ms)
    {
        switch (m_health)
        {
            case Health::Initializing:
                // Startup grace is host/target driven; Ready is granted by the
                // harness when grace + requalification complete (slice: set_ready()).
                break;
            case Health::Ready:
                if (sample_age_ms > T_fresh_ms)
                {
                    enter_degraded(now); // HZ-01 class: sensing freshness loss
                }
                break;
            case Health::Degraded:
                if (sample_age_ms > T_fresh_ms)
                {
                    if (now - m_degraded_since_ms >= T_deg_ms)
                    {
                        m_health = Health::Fault; // FAULT_DEGRADED_TIMEOUT class
                    }
                }
                else
                {
                    // Qualified recovery: fresh samples restore Ready.
                    m_health = Health::Ready;
                }
                break;
            case Health::Fault:
                break; // latched; recovery requires explicit reset (slice: clear_fault())
            default:
                break; // defensive (R4): unknown enum value never changes state
        }
    }

    void set_ready() { m_health = Health::Ready; }
    void latch_fault() { m_health = Health::Fault; }
    void clear_fault()
    {
        if (m_health == Health::Fault)
        {
            m_health = Health::Initializing; // requalify before Ready
        }
    }

    Health health() const { return m_health; }
    bool motion_allowed() const { return m_health == Health::Ready; }

  private:
    void enter_degraded(std::uint64_t now)
    {
        m_health = Health::Degraded;
        m_degraded_since_ms = now;
    }

    Health m_health = Health::Initializing;
    std::uint64_t m_degraded_since_ms = 0;
};

} // namespace slice
