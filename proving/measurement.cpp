#include "proving/measurement.h"

#include <cstdio>

namespace slice
{
namespace proving
{

namespace
{

void print_workload(const Workload& wl)
{
    std::printf("  workload: scenario=%s run=%u can_flood=%d uart_flood=%d log_storm=%d "
                "i2c_stuck=%d flash=%d irq_hz=%u steps_per_tick=%u bms_parallel=%d\n",
                wl.scenario_id ? wl.scenario_id : "?", wl.run_number, wl.can_flood ? 1 : 0,
                wl.uart_flood ? 1 : 0, wl.log_storm ? 1 : 0, wl.i2c_stuck ? 1 : 0,
                wl.flash_active ? 1 : 0, wl.interrupt_load_hz, wl.operation_steps_per_tick,
                wl.bms_parallel ? 1 : 0);
}

} // namespace

void Measurement::finalize()
{
    std::printf("=== measurement record ===\n");
    print_workload(m_wl);
    for (std::uint32_t i = 0; i < kMetricCount; ++i)
    {
        if (m_metrics[i].count() > 0)
        {
            std::printf("  metric#%02u: n=%u max_us=%llu avg_us=%llu\n", i, m_metrics[i].count(),
                        static_cast<unsigned long long>(m_metrics[i].max_us()),
                        static_cast<unsigned long long>(m_metrics[i].avg_us()));
        }
    }
    if (m_trace_count > 0)
    {
        std::uint64_t worst_us = 0;
        std::uint32_t worst = 0;
        for (std::uint32_t i = 0; i < m_trace_count; ++i)
        {
            if (m_traces[i].delta_us > worst_us)
            {
                worst_us = m_traces[i].delta_us;
                worst = i;
            }
        }
        const Trace& t = m_traces[worst];
        std::printf("  worst trace: %s trigger_us=%llu output_us=%llu delta_us=%llu (n=%u)\n",
                    t.scenario_id, static_cast<unsigned long long>(t.trigger_us),
                    static_cast<unsigned long long>(t.output_us),
                    static_cast<unsigned long long>(t.delta_us), m_trace_count);
    }
    std::printf("=== end record ===\n");
}

} // namespace proving
} // namespace slice
