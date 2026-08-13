// I2C bus adapter implementation (target-only, Arduino Core; design
// docs/sensing-slice-design-v3.md section 4.2). Bounded transactions only
// (one read per slot, <= T_step); never called from an ISR (rule R2).
//
// Transaction (V1 TOF_Sense.cpp pattern): write reg (STOP), restart, read
// len (STOP). STM32duino collapses every read-phase failure to zero, so a
// zero requestFrom result is classified Short (under-read), not Stuck - one
// such result is not evidence the whole shared bus needs a reset (V1).
#include "adapters/i2c_bus.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <Wire.h>

namespace v3
{
namespace
{

// STM32duino Wire endTransmission()/i2c_master_write statuses:
// 0 success, 1 data-too-long, 2 NACK-addr, 3 NACK-data, 4 busy/timeout/error.
constexpr std::uint8_t kWireOk = 0u;
constexpr std::uint8_t kWireNackAddress = 2u;
constexpr std::uint8_t kWireNackData = 3u;

// SDA/SCL wiring (bench record #73): PB11 (SDA), PB10 (SCL) = I2C2 (AF4).
constexpr std::uint8_t kSdaPin = PB11;
constexpr std::uint8_t kSclPin = PB10;
constexpr std::uint32_t kBusHz = 100000u;

} // namespace

void I2cBus::init()
{
    Wire.begin(kSdaPin, kSclPin);
    Wire.setClock(kBusHz);
}

I2cResult I2cBus::read(std::uint8_t device, std::uint8_t reg,
                       std::uint8_t* out, std::uint8_t len)
{
    if (out == nullptr || len == 0u)
    {
        return I2cResult::Short;
    }

    Wire.beginTransmission(device);
    if (Wire.write(reg) != 1u)
    {
        m_last_wire_status = 1u; // write-phase failure (data-too-long or worse)
        (void)Wire.endTransmission(true);
        return I2cResult::Stuck;
    }
    const std::uint8_t tx = Wire.endTransmission(true);
    m_last_wire_status = tx;
    if (tx != kWireOk)
    {
        if (tx == kWireNackAddress || tx == kWireNackData)
        {
            return I2cResult::NoAck;
        }
        // BUSY/TIMEOUT/ERROR/data-too-long: bus-level, recover candidate.
        return I2cResult::Stuck;
    }

    const std::uint8_t requested = Wire.requestFrom(device, len);
    std::uint8_t received = 0u;
    while (Wire.available() > 0 && received < len)
    {
        out[received++] = static_cast<std::uint8_t>(Wire.read());
    }
    while (Wire.available() > 0)
    {
        (void)Wire.read();
    }
    if (requested == 0u || requested != len || received != len)
    {
        return I2cResult::Short;
    }
    return I2cResult::Ok;
}

I2cResult I2cBus::recover()
{
    // Reinit the peripheral and the pins; STM32duino begin() runs its bus
    // recovery (<= 16 SCL pulses, STOP) for a master before starting, which
    // satisfies the obligation #14 reinit + pulse mechanic. The >= 5 s
    // cooldown between attempts is enforced by the Sensing Service.
    Wire.end();
    Wire.begin(kSdaPin, kSclPin);
    Wire.setClock(kBusHz);
    m_last_wire_status = 0u;
    return I2cResult::Recovered;
}

} // namespace v3

#endif // ARDUINO
