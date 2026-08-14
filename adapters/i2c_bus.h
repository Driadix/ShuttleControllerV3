// I2C bus adapter: implements v3::I2cPort for the target (design
// docs/sensing-slice-design-v3.md section 4.2; ticket #63). Arduino API is
// confined to this adapter (issue #51 section 5). Bus wiring per bench
// record #73: I2C on PB11 (SDA) / PB10 (SCL) at 100 kHz - on STM32F405 this
// is I2C2 (AF4), which STM32duino selects from the pins passed to begin().
//
// Status classification follows V1 TOF_Sense.cpp: NACK (address/data) and
// under-read are device-level outcomes (no bus recovery); HAL
// BUSY/TIMEOUT/ERROR is a bus-level outcome (Stuck -> recover candidate).
// recover() = manual open-drain recovery (<= 16 SCL pulses + STOP, obligation
// #14) + Wire reinit when the bus is released; the >= 5 s cooldown lives in
// the Sensing Service (domain), not here.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class I2cBus : public I2cPort
{
  public:
    void init(); // Wire.begin(PB11, PB10), 100 kHz; foreground, startup

    I2cResult read(std::uint8_t device, std::uint8_t reg,
                   std::uint8_t* out, std::uint8_t len) override;
    I2cResult recover() override;
    std::uint8_t last_wire_status() const override { return m_last_wire_status; }

  private:
    std::uint8_t m_last_wire_status = 0;
};

} // namespace v3
