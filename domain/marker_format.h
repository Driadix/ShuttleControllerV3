// Формат persisted crash-маркера (design docs/safety-authority-design-v3.md section 2.3,
// #45 Q5 A). Одно 32-bit слово Backup SRAM: биты [31:16] = CRC16(payload),
// биты [15:0] = payload (bit 15 = crash_pending, биты [14:0] = crash_count).
// Запись - один выровненный 32-bit store (атомарен на Cortex-M4): payload+CRC
// не могут быть записаны «наполовину». Чистые функции, host-тестируемы.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{
namespace safety
{

// Состояние маркера = тип порта SafetyStateMarker::State (единый доменный тип).
using MarkerState = SafetyStateMarker::State;

// CRC16-CCITT (poly 0x1021, init 0xFFFF) над 16-bit payload.
inline std::uint16_t marker_crc16(std::uint16_t payload)
{
    std::uint16_t crc = 0xFFFFu;
    for (int bit = 0; bit < 16; ++bit)
    {
        const std::uint16_t carry = static_cast<std::uint16_t>((crc & 0x8000u) ? 1u : 0u);
        crc = static_cast<std::uint16_t>(crc << 1);
        if (carry)
        {
            crc ^= 0x1021u;
        }
        if ((payload & 0x8000u) != 0u)
        {
            crc ^= 0x1021u;
        }
        payload = static_cast<std::uint16_t>(payload << 1);
    }
    return crc;
}

inline std::uint16_t marker_payload(const MarkerState& s)
{
    std::uint16_t p = static_cast<std::uint16_t>(s.crash_count & 0x7FFFu);
    if (s.crash_pending)
    {
        p = static_cast<std::uint16_t>(p | 0x8000u);
    }
    return p;
}

// Кодирование в одно 32-bit слово (биты [15:0] payload, [31:16] CRC16).
inline std::uint32_t marker_encode(const MarkerState& s)
{
    const std::uint16_t payload = marker_payload(s);
    const std::uint32_t crc = marker_crc16(payload);
    return (crc << 16u) | payload;
}

// Декодирование. CRC-fail трактуется permissive («краха не было», решение владельца):
// первый boot BKP-домен содержит мусор - fail-safe-трактовка блокировала бы устройство
// ложным Fault до Service; однословный атомарный store делает CRC-fail после init-штампа
// недостижимым в нормальной работе.
inline MarkerState marker_decode(std::uint32_t word)
{
    const std::uint16_t payload = static_cast<std::uint16_t>(word & 0xFFFFu);
    const std::uint16_t crc = static_cast<std::uint16_t>((word >> 16u) & 0xFFFFu);
    if (marker_crc16(payload) != crc)
    {
        return MarkerState{};
    }
    MarkerState s;
    s.crash_pending = (payload & 0x8000u) != 0u;
    s.crash_count = payload & 0x7FFFu;
    return s;
}

} // namespace safety
} // namespace v3
