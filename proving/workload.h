// Workload metadata for observed-maxima records (issue #52 section 6.3:
// observed maxima with workload metadata; issue 10 evidence #3).
// Fixed-size, no dynamic allocation (rule R1).
#pragma once

#include <cstdint>

namespace slice
{
namespace proving
{

// Fixed workload matrix describing one measurement run. Every observed max is
// tied to exactly one Workload record - maxima are meaningless without it.
struct Workload
{
    const char* scenario_id = nullptr; // e.g. "O1-eso", "O3-flash", "O13-flood"
    std::uint32_t run_number = 0;

    // Active load generators (issue 10 mandatory loads).
    bool can_flood = false;          // L1: RX > 64 frames/tick
    bool uart_flood = false;         // L3: continuous/malformed RX
    bool log_storm = false;          // L4: max logs/events emission
    bool i2c_stuck = false;          // L5: stuck SCL/SDA injection
    bool flash_active = false;       // L6: sector erase + page program
    std::uint32_t interrupt_load_hz = 0; // L8: periodic ISR rate
    std::uint32_t operation_steps_per_tick = 0; // L7: synthetic steps per tick
    bool bms_parallel = false;       // L9: BMS transactions on shared I2C
};

} // namespace proving
} // namespace slice
