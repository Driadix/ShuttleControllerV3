// Shared host fakes for the execution foundation tests (design
// docs/execution-foundation-design-v3.md section 7.2). Deterministic:
// TestTimeSource drives time explicitly; RecordingEvents captures kernel
// events; FakeWatchdog models the 6.8-18.8 s IWDG window; FakeSafety counts
// tick() calls and intervals; FakeResetCause returns a preset cause.
// Fixed-size capture buffers (no std::vector): tests compile with
// -fno-exceptions (rule R1 spirit, dev-only).
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace testfakes
{

// Deterministic TimeSource: host tests advance time explicitly (no test hooks
// in the production API; the injection lives in this fake, design section 2.2).
class TestTimeSource : public v3::TimeSource
{
  public:
    void init_tick() override { m_inited = true; }
    std::uint64_t raw_now_ms() override { return m_now_ms; }
    std::uint64_t raw_ticks_us() override { return m_ticks_us; }

    void set_time_ms(std::uint64_t t)
    {
        m_now_ms = t;
        m_ticks_us = t * 1000; // host: us follows ms (deterministic, no real timing)
    }
    void advance_ms(std::uint64_t dt) { set_time_ms(m_now_ms + dt); }
    // Advances ticks_us only: simulates a step that consumed CPU time without
    // moving the wall clock (T1: step > T_step inside one tick).
    void advance_us(std::uint64_t du) { m_ticks_us += du; }

    bool inited() const { return m_inited; }

  private:
    bool m_inited = false;
    std::uint64_t m_now_ms = 0;
    std::uint64_t m_ticks_us = 0;
};

// Recording kernel-event sink (T13, T14, T1, T7). Fixed capture buffers.
class RecordingEvents : public v3::KernelEvents
{
  public:
    static constexpr std::uint32_t kMaxCapture = 64;

    void step_overrun(std::uint32_t step_ms) override
    {
        if (m_overrun_count < kMaxCapture)
        {
            m_overruns[m_overrun_count++] = step_ms;
        }
    }
    void scheduler_gap(std::uint64_t gap_ms) override
    {
        if (m_gap_count < kMaxCapture)
        {
            m_gaps[m_gap_count++] = gap_ms;
        }
    }
    void schedule_rejected() override { ++m_rejected; }
    void reset_cause(v3::ResetCause cause) override
    {
        if (m_cause_count < kMaxCapture)
        {
            m_causes[m_cause_count++] = cause;
        }
    }

    std::uint32_t overrun_count() const { return m_overrun_count; }
    std::uint32_t gap_count() const { return m_gap_count; }
    std::uint32_t cause_count() const { return m_cause_count; }
    std::uint32_t rejected() const { return m_rejected; }

    std::uint32_t overrun(std::uint32_t i) const { return m_overruns[i]; }
    std::uint64_t gap(std::uint32_t i) const { return m_gaps[i]; }
    v3::ResetCause cause(std::uint32_t i) const { return m_causes[i]; }

    void clear()
    {
        m_overrun_count = 0;
        m_gap_count = 0;
        m_rejected = 0;
        m_cause_count = 0;
    }

  private:
    std::uint32_t m_overruns[kMaxCapture] = {};
    std::uint32_t m_gap_count = 0;
    std::uint64_t m_gaps[kMaxCapture] = {};
    std::uint32_t m_overrun_count = 0;
    std::uint32_t m_rejected = 0;
    v3::ResetCause m_causes[kMaxCapture] = {};
    std::uint32_t m_cause_count = 0;
};

// Watchdog hardware fake: counts reloads and models the IWDG window on the
// fast LSI end (6.8 s, issue #48 section 3) against the injected clock
// (T6 idle reload, T11 F5 starvation, T12 flash windows).
class FakeWatchdog : public v3::WatchdogPort
{
  public:
    explicit FakeWatchdog(TestTimeSource& ts) : m_ts(&ts) {}

    void init(std::uint32_t window_us) override
    {
        m_window_us = window_us;
        m_inited = true;
        m_last_reload_ms = m_ts->raw_now_ms(); // armed at startup
    }
    void reload() override
    {
        ++m_reload_count;
        m_last_reload_ms = m_ts->raw_now_ms();
    }

    std::uint32_t reload_count() const { return m_reload_count; }
    bool inited() const { return m_inited; }
    std::uint32_t window_us() const { return m_window_us; }

    // Starvation model: reload was not called for >= fast-end window.
    bool starved() const { return (m_ts->raw_now_ms() - m_last_reload_ms) >= 6'800; }

  private:
    TestTimeSource* m_ts;
    bool m_inited = false;
    std::uint32_t m_window_us = 0;
    std::uint32_t m_reload_count = 0;
    std::uint64_t m_last_reload_ms = 0;
};

// SafetySlot fake: counts tick() calls and records the max interval between
// consecutive ticks (T17 INV-SENSING-FRESH under full backlog).
class FakeSafety : public v3::SafetySlot
{
  public:
    void tick(std::uint64_t now) override
    {
        if (m_tick_count > 0)
        {
            const std::uint64_t interval = now >= m_last_tick_ms ? now - m_last_tick_ms : 0;
            if (interval > m_max_interval_ms)
            {
                m_max_interval_ms = interval;
            }
        }
        m_last_tick_ms = now;
        ++m_tick_count;
    }

    std::uint32_t tick_count() const { return m_tick_count; }
    std::uint64_t max_interval_ms() const { return m_max_interval_ms; }
    std::uint64_t last_tick_ms() const { return m_last_tick_ms; }

  private:
    std::uint32_t m_tick_count = 0;
    std::uint64_t m_max_interval_ms = 0;
    std::uint64_t m_last_tick_ms = 0;
};

// Reset-cause fake: preset cause + read counter (T14 startup order).
class FakeResetCause : public v3::ResetCauseSource
{
  public:
    explicit FakeResetCause(v3::ResetCause cause) : m_cause(cause) {}

    v3::ResetCause read() override
    {
        ++m_read_count;
        return m_cause;
    }

    std::uint32_t read_count() const { return m_read_count; }

  private:
    v3::ResetCause m_cause;
    std::uint32_t m_read_count = 0;
};

} // namespace testfakes
