// Доменное диагностическое зеркало `.bram_safety` (design
// docs/safety-authority-design-v3.md section 2.5; L4-наблюдаемость, паттерн #63).
// Framework-free (stdint + доменные типы), include-lint чист. pinned-инстанс
// объявляется в platform (linker-секция .bram_safety); домен/адаптеры получают
// указатель SafetyDiag* в init и пишут поля напрямую (foreground, one thread).
//
// Single-writer по полям:
//   - SafetyAuthority: health / degraded_class / fault / state_entry_ms /
//     degraded_motion_ms / current_intent / activity_intents_rejected /
//     stops_issued / force_stops_issued
//   - ActuatorController: frame records kind 0 (100/101) / kind 1 (zero)
//   - CAN adapter: can_tx_count / can_tx_dropped / can_rx_dropped /
//     can_bus_off_recoveries / can_state / frame records kind 2 (force-stop)
#pragma once

#include <cstdint>

#include "domain/ports.h"
#include "domain/safety_intent.h"
#include "domain/safety_state.h"

namespace v3
{

struct SafetyDiag
{
    std::uint32_t magic = 0;                 // 'SAF1'
    std::uint32_t version = 1;               // структуры
    std::uint64_t uptime_ms = 0;             // monotonic
    safety::SafetyHealth health = safety::SafetyHealth::Initializing;
    safety::DegradedClass degraded_class = safety::DegradedClass::None;
    safety::SafetyFault fault = safety::SafetyFault::None;
    std::uint64_t state_entry_ms = 0;        // monotonic момент входа в текущее состояние
    std::uint64_t degraded_motion_ms = 0;    // накопленное время в motion-capable Degraded
                                             //   (T_deg-отсчёт, #48 §2.2)
    safety::Intent current_intent;
    std::uint32_t activity_intents_rejected = 0;
    std::uint32_t stops_issued = 0;
    std::uint32_t force_stops_issued = 0;
    // CAN (workload metadata) - владелец адаптер:
    std::uint32_t can_tx_count = 0;
    std::uint32_t can_tx_dropped = 0;
    std::uint32_t can_rx_dropped = 0;
    CanErrorState can_state = CanErrorState::Active;
    std::uint32_t can_bus_off_recoveries = 0;

    // Ring последних кадров (raw timestamps, bounded). kind: 0=100/101, 1=zero, 2=force-stop.
    struct FrameRecord
    {
        std::uint64_t tx_ms = 0;
        std::uint32_t id = 0;
        std::uint8_t data[8] = {};
        std::uint8_t len = 0;
        std::uint8_t kind = 0;
    };
    FrameRecord frames[16] = {};
    std::uint32_t frame_head = 0; // индекс последней записи (кольцо)

    // Запись кадра в ring (foreground, one thread). Вызывается владельцем кадра
    // (ActuatorController для 100/101/zero; CAN adapter для force-stop).
    void record_frame(std::uint8_t kind, const CanFrame& f, std::uint64_t now)
    {
        frame_head = (frame_head + 1u) & 15u;
        FrameRecord& r = frames[frame_head];
        r.tx_ms = now;
        r.id = f.id;
        r.len = f.len;
        r.kind = kind;
        for (std::uint32_t i = 0; i < 8u; ++i)
        {
            r.data[i] = f.data[i];
        }
    }
};

} // namespace v3
