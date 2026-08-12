#include "proving/sim.h"

#include <cstdint>

namespace slice
{
namespace proving
{
namespace sim
{

std::uint64_t can_rx_drain(CpuClock& clk, std::uint32_t frames)
{
    const std::uint64_t cost = static_cast<std::uint64_t>(frames) * kCanRxDrainUsPerFrame;
    clk.add(cost);
    return cost;
}

std::uint64_t can_tx(CpuClock& clk, std::uint32_t frames)
{
    const std::uint64_t cost = static_cast<std::uint64_t>(frames) * kCanTxUs;
    clk.add(cost);
    return cost;
}

std::uint64_t uart_drain(CpuClock& clk, std::uint32_t bytes, std::uint32_t baud_bits_per_byte)
{
    // Driver drain cost per byte; the wire-time (baud-derived) models when
    // bytes become available, not CPU consumption (DMA / producer-budget).
    (void)baud_bits_per_byte;
    const std::uint64_t cost = static_cast<std::uint64_t>(bytes) * kUartDrainUsPerByte;
    clk.add(cost);
    return cost;
}

std::uint64_t i2c_tof_read(CpuClock& clk)
{
    // The Arduino Wire read blocks for the whole transaction (V1-class,
    // no timeout); V3's bounded async contract must still fit T_step.
    const std::uint64_t cost = kI2cTofReadUs + kI2cOverheadUs;
    clk.add(cost);
    return cost;
}

std::uint64_t flash_erase(CpuClock& clk)
{
    // Quiescent atomic window: the only allowed T_step exception (issue #48
    // section 4). The watchdog fast-end (6.8 s) must still hold.
    clk.add(kFlashEraseUs);
    return kFlashEraseUs;
}

std::uint64_t flash_program(CpuClock& clk)
{
    clk.add(kFlashProgramUs);
    return kFlashProgramUs;
}

} // namespace sim
} // namespace proving
} // namespace slice
