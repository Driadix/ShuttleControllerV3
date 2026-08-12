// Host-simulation with physically grounded parameters (issue #54, host-only
// scope; design doc section 14). Models the peripheral CPU-cost that a bounded
// step consumes, using datasheet/code-derived numbers (issue #48 section 1:
// analytical estimates are never called measured).
//
// Model distinction:
// - wire-time: physical transfer duration (CAN frame, UART byte, I2C
//   transaction). Does NOT consume CPU except where the driver blocks (the
//   Arduino Wire read blocks, V1-class - modelled as CPU-cost below).
// - CPU-cost: driver time inside the step (FIFO drain, mailbox write, blocking
//   I2C read, flash erase/program window).
//
// Budgets checked against issue #48: T_step = 10 ms (exception: quiescent
// flash window <= 4 s, watchdog fast-end 6.8 s holds), T_eso <= 70 ms,
// T_fresh + T_eso <= 370 ms.
#pragma once

#include <cstdint>

namespace slice
{
namespace proving
{
namespace sim
{

// ---- physically grounded constants (sources in the design doc section 14) ----

// CAN 500 kbit/s, extended frame: 44 bits overhead + 8 bytes payload = 108 bits.
constexpr std::uint64_t kCanFrameUs = (44 + 8 * 8) * 1000 / 500; // 216 us
// UART byte = 10 bits (8E1), display 230400.
constexpr std::uint64_t kUartDisplayByteUs = 10 * 1000000 / 230400; // ~43 us
// UART radio 57600.
constexpr std::uint64_t kUartRadioByteUs = 10 * 1000000 / 57600; // ~174 us
// I2C 100 kHz, ToF read of 13 bytes: 13 * 9 bits (addr+data+ack) + overhead.
constexpr std::uint64_t kI2cTofReadUs = 13 * 9 * 10 + 40; // ~1210 us (V1 ~1.2-1.5 ms class)
// Flash: sector 7 erase worst case (DS8626 Table 40), page program 512 B.
constexpr std::uint64_t kFlashEraseUs = 4'000'000;  // 4 s (x8, 128 KB sector)
constexpr std::uint64_t kFlashProgramUs = 12'800;   // 128 words x 100 us

// Driver CPU-cost per operation (model calibration, conservative).
constexpr std::uint64_t kCanRxDrainUsPerFrame = 2; // FIFO pop + dispatch O(1)
constexpr std::uint64_t kCanTxUs = 1;              // mailbox write
constexpr std::uint64_t kUartDrainUsPerByte = 1;   // FIFO pop
constexpr std::uint64_t kI2cOverheadUs = 50;       // bus monitor + addressing

// Accumulates the virtual CPU-time consumed by a step. Host-simulation only:
// the numbers are model inputs, not measurements.
class CpuClock
{
  public:
    void add(std::uint64_t us) { m_us += us; }
    std::uint64_t consumed_us() const { return m_us; }
    void reset() { m_us = 0; }

  private:
    std::uint64_t m_us = 0;
};

// Modelled peripheral operations; each returns the CPU-cost it consumes.
// The step calls these and the harness records metric(8) (bounded steps).

// CAN RX drain of `frames` frames (budget <= 64 frames/tick, issue #48 s7).
std::uint64_t can_rx_drain(CpuClock& clk, std::uint32_t frames);

// CAN TX of `frames` frames (budget <= 16 frames/tick).
std::uint64_t can_tx(CpuClock& clk, std::uint32_t frames);

// UART RX drain of `bytes` (display budget 230 B/tick).
std::uint64_t uart_drain(CpuClock& clk, std::uint32_t bytes, std::uint32_t baud_bits_per_byte);

// Blocking I2C ToF read (Wire blocks, V1-class): wire-time = CPU-cost.
std::uint64_t i2c_tof_read(CpuClock& clk);

// Flash erase window (quiescent, the only allowed T_step exception).
std::uint64_t flash_erase(CpuClock& clk);

// Flash page program (512 B).
std::uint64_t flash_program(CpuClock& clk);

} // namespace sim
} // namespace proving
} // namespace slice
