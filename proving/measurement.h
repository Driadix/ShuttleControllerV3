// Measurement recorder for the proving slice (issue #52 section 6.3:
// observed maxima protocol; issue 10 evidence #3).
//
// Records per-metric observed maxima with the active workload, plus
// event->safe-output traces (trigger -> output). Output is a fixed-size
// text/JSON-ish record; no dynamic allocation. Host: prints to stdout for the
// report. Target: prints to the diagnostic serial port.
#pragma once

#include <cstdint>

#include "proving/workload.h"

namespace slice
{
namespace proving
{

class Metric
{
  public:
    void record(std::uint64_t us)
    {
        ++m_count;
        m_sum_us += us;
        if (us > m_max_us)
        {
            m_max_us = us;
        }
    }
    std::uint32_t count() const { return m_count; }
    std::uint64_t max_us() const { return m_max_us; }
    std::uint64_t avg_us() const { return m_count == 0 ? 0 : m_sum_us / m_count; }

  private:
    std::uint32_t m_count = 0;
    std::uint64_t m_max_us = 0;
    std::uint64_t m_sum_us = 0;
};

// Event->safe-output trace: trigger timestamp -> safe-output timestamp.
struct Trace
{
    const char* scenario_id = nullptr;
    std::uint64_t trigger_ms = 0;
    std::uint64_t output_ms = 0;
    std::uint64_t delta_us = 0;
};

class Measurement
{
  public:
    void begin_run(const Workload& wl) { m_wl = wl; }

    // Named metrics (obligation IDs #43 section 8).
    Metric& metric(std::uint32_t obligation_id) { return m_metrics[obligation_id % kMetricCount]; }

    void record_trace(std::uint64_t trigger_ms, std::uint64_t output_ms)
    {
        if (m_trace_count < kMaxTraces)
        {
            m_traces[m_trace_count] = Trace{m_wl.scenario_id, trigger_ms, output_ms,
                                            (output_ms - trigger_ms) * 1000};
            ++m_trace_count;
        }
    }

    // Prints the run record (workload + observed maxima + worst trace).
    void finalize();

  private:
    static constexpr std::uint32_t kMetricCount = 16; // obligations #1..#15 + spare
    static constexpr std::uint32_t kMaxTraces = 64;

    Workload m_wl = {};
    Metric m_metrics[kMetricCount] = {};
    Trace m_traces[kMaxTraces] = {};
    std::uint32_t m_trace_count = 0;
};

} // namespace proving
} // namespace slice
