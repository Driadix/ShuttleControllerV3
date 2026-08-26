#pragma once
#include <cstdint>
#include <array>

namespace v3::observability {

// §2.1 Envelope - база для всех классов наблюдаемости
struct Envelope {
    uint8_t classId;           // 0: telemetry, 1: events, 2: logs, 3: traces
    uint32_t controllerEpoch;  // Fencing boundary
    uint32_t monotonicTick;    // Порядок
    uint8_t seq;               // Rolling счётчик
};

// §2.1 Time Metadata
enum class TimeValidity : uint8_t { Unsynced = 0, RtcOnly = 1, Synced = 2 };

// §2.2 TelemetryRecord
struct TelemetryRecord {
    Envelope env;
    uint8_t opState;
    int32_t position_mm;
    int32_t speed_mm_s;
    uint8_t health;
    uint16_t faultMask;
    uint16_t warningMask;
    uint16_t batteryCharge;
    uint16_t batteryVoltage_mV;
    uint16_t palletCount;
    uint8_t stateFlags;
};

// §2.3 EventRecord
struct EventRecord {
    Envelope env;
    uint32_t wallTime;
    TimeValidity timeValidity;
    uint16_t eventId;
    uint8_t severity;
    // Bounded context (implementation specific, usually POD struct)
    std::array<uint8_t, 16> context; 
};

// §2.4 LogRecord
struct LogRecord {
    Envelope env;
    uint32_t wallTime;
    TimeValidity timeValidity;
    uint8_t level;
    uint8_t moduleId;
    std::array<char, 80> text; // Ограничение по дизайну §2.4
};

// §2.5 TraceRecord
// NOTE: Simplified context fragment (256B) vs 512B staging area per design doc
struct TraceRecord {
    Envelope env;
    uint32_t wallTime;
    TimeValidity timeValidity;
    uint8_t kind; // fault_capture, dev_timeline
    // Контекст захвата фолта (staging 512B)
    std::array<uint8_t, 256> context_fragment; // Упрощено: 256B вместо 512B из дизайна
};

} // namespace v3::observability
