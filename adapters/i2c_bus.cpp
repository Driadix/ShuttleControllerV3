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
constexpr std::uint32_t kSdaMask = 1u << 11;
constexpr std::uint32_t kSclMask = 1u << 10;

// Idle I2C lines are both high. A low line means the bus is held (e.g.
// unpowered sensors pulling SDA/SCL down - the bench 5V-side loss, design
// section 10): skip the Wire transaction entirely and report Stuck, because
// STM32duino busy-spins up to I2C_TIMEOUT_TICK (100 ms) inside
// endTransmission/requestFrom on a held bus - an unbounded step violation.
bool bus_idle()
{
    return (GPIOB->IDR & (kSdaMask | kSclMask)) == (kSdaMask | kSclMask);
}

// Manual open-drain SCL toggling for recover() (obligation #14: <= 16 SCL
// pulses). PB10 (SCL) / PB11 (SDA) are switched to GPIO output open-drain;
// AFR (AF4) is restored afterwards so Wire keeps working.
void gpio_output_od(bool enabled)
{
    if (enabled)
    {
        GPIOB->MODER = (GPIOB->MODER & ~(0x3u << 20 | 0x3u << 22)) |
                       (0x1u << 20) | (0x1u << 22); // PB10/PB11 -> output
        GPIOB->OTYPER |= (1u << 10) | (1u << 11);   // open-drain
        GPIOB->ODR |= (1u << 10) | (1u << 11);      // lines released (pull-ups)
    }
    else
    {
        GPIOB->MODER = (GPIOB->MODER & ~(0x3u << 20 | 0x3u << 22)) |
                       (0x2u << 20) | (0x2u << 22); // PB10/PB11 -> AF (AF4 kept)
    }
}

} // namespace

void I2cBus::init()
{
    // Init in the WORKING V1 style (Cntrl_V2 initTofI2cBus, proven on the
    // bench 2026-08-13): setSDA/setSCL + begin() + setClock. The earlier
    // Wire.begin(PB11, PB10) form produced tx=4 (Stuck) on the very same
    // bus with the same pins - empirically broken on this core (4.20701.0),
    // documented in the design doc section 10. PB11/PB10 are Arduino-style
    // pin numbers in the generic F405RG variant (PB11=27, PB10=26), so the
    // setSDA(PinName) overload is not selected; the observable difference
    // is the init call style, not the pins (verified: GPIO AF4/OD/IDR=1).
    Wire.setSDA(kSdaPin);
    Wire.setSCL(kSclPin);
    Wire.begin();
    Wire.setClock(kBusHz);
}

I2cResult I2cBus::read(std::uint8_t device, std::uint8_t reg,
                       std::uint8_t* out, std::uint8_t len)
{
    if (out == nullptr || len == 0u)
    {
        return I2cResult::Short;
    }
    if (!bus_idle())
    {
        // Bus held low (stuck): report Stuck without a blocking Wire call -
        // the Sensing Service schedules recover() with its cooldown.
        m_last_wire_status = 4u; // HAL busy/error class
        return I2cResult::Stuck;
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
    // Manual open-drain recovery (obligation #14: <= 16 SCL pulses, then
    // STOP). Toggle SCL up to 16 times, checking SDA after each pulse; emit
    // a STOP once SDA is released; only then reinit Wire - its recoverBus
    // sees SDA high and emits 0 pulses. If SDA is still held after 16
    // pulses: restore the AF pins and return Stuck WITHOUT Wire.begin()
    // (the next attempt is gated by the Sensing Service cooldown).
    gpio_output_od(true);

    bool released = false;
    for (std::uint32_t i = 0; i < 16u && !released; ++i)
    {
        GPIOB->ODR &= ~kSclMask; // SCL low
        delayMicroseconds(5);
        GPIOB->ODR |= kSclMask;  // SCL high (released through the pull-ups)
        delayMicroseconds(5);
        released = (GPIOB->IDR & kSdaMask) != 0u; // SDA high => bus free
    }

    if (!released)
    {
        gpio_output_od(false);
        m_last_wire_status = 4u; // HAL busy/error class
        return I2cResult::Stuck;
    }

    // STOP: SCL low, SDA low; release SCL (verify it went high), then
    // release SDA while SCL stays high - the SDA low->high edge with SCL
    // high is the STOP condition. If SCL did not release, the bus is still
    // stuck: return Stuck without reinit.
    GPIOB->ODR &= ~kSclMask; // SCL low
    GPIOB->ODR &= ~kSdaMask; // SDA low
    delayMicroseconds(5);
    GPIOB->ODR |= kSclMask;  // release SCL
    delayMicroseconds(5);
    if ((GPIOB->IDR & kSclMask) == 0u)
    {
        gpio_output_od(false);
        m_last_wire_status = 4u; // HAL busy/error class
        return I2cResult::Stuck;
    }
    GPIOB->ODR |= kSdaMask;  // release SDA while SCL high -> STOP edge
    delayMicroseconds(5);

    gpio_output_od(false);
    Wire.end();
    Wire.begin(kSdaPin, kSclPin); // SDA high on entry => recoverBus emits 0
    Wire.setClock(kBusHz);
    m_last_wire_status = 0u;
    return I2cResult::Recovered;
}

} // namespace v3

#endif // ARDUINO
