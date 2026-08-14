// CAN кадровый контракт слайса (design docs/safety-authority-design-v3.md section 4.3).
// Контракт адаптера (#43 §4 «содержимое фиксируется в контракте адаптера»):
//   - Traction velocity: extended 0x100, DLC 8, byte0 = направление, bytes1-2 = |velocity|
//   - Lifter: extended 0x101, DLC 8, byte0 = up/down, bytes1-2 = скорость
//   - Нулевой кадр (stop): 0x100, все нули
//   - Force-stop: extended 0x00000001 (минимальный ID), DLC 8, 0xFF x 8
// 100/101 wire-формат - provisional (V1 evidence index: точный формат требует
// схемы/drive documentation; rebaseline при появлении drive-доков).
// Чистые функции построения кадров, host-тестируемы.
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/safety_intent.h"

namespace v3
{
namespace safety
{

constexpr std::uint32_t kCanTractionId = 0x100u;
constexpr std::uint32_t kCanLiftId = 0x101u;
constexpr std::uint32_t kForceStopId = 0x00000001u; // минимальный extended ID на шине

// Force-stop кадр (INV-FORCE-STOP-CHANNEL, #43 §4): min extended ID + максим. stop-индикатор.
constexpr CanFrame kForceStopFrame = {kForceStopId, {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu}, 8u};

// Нулевой кадр (stop-профили IMMEDIATE/FORCE-STOP, #45 §4): 0x100, velocity 0.
inline CanFrame zero_frame()
{
    CanFrame f;
    f.id = kCanTractionId;
    f.len = 8;
    return f;
}

// Intent -> кадр 100/101 (provisional контракт §4.3). Lift -> 0x101, velocity -> 0x100.
inline CanFrame build_actuator_frame(const Intent& intent)
{
    CanFrame f;
    f.len = 8;
    const std::int16_t v = intent.velocity;
    const std::uint16_t mag = v < 0 ? static_cast<std::uint16_t>(-static_cast<std::int32_t>(v))
                                    : static_cast<std::uint16_t>(v);
    if (intent.kind == IntentKind::Lift)
    {
        f.id = kCanLiftId;
        f.data[0] = v >= 0 ? 1u : 0u; // up/down (provisional)
    }
    else // VelocitySetpoint -> traction
    {
        f.id = kCanTractionId;
        f.data[0] = v >= 0 ? 1u : 0u; // направление (provisional)
    }
    f.data[1] = static_cast<std::uint8_t>((mag >> 8u) & 0xFFu);
    f.data[2] = static_cast<std::uint8_t>(mag & 0xFFu);
    return f;
}

} // namespace safety
} // namespace v3
