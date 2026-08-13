// Sensing Service (design docs/sensing-slice-design-v3.md, ticket #63).
// Domain-side, framework-free (rule R6 spirit, issue #51 section 5): pure
// C++ (stdint only), depends only on domain/ports.h (v3::I2cPort). The
// composition root (platform/main.cpp) schedules the bounded step via
// kernel::schedule and passes `now` in; the service never touches the
// kernel, Arduino or adapters (dependencies point inward, issue #43).
//
// Ownership (issue #43 section 4, #48 section 7): the Sensing Service is the
// single I2C slot-schedule owner in Phase 1. It acquires ToF (round-robin,
// one 8 ms slot per read, 4 sensors at 0x09..0x0C) and AS5600 (every 250 ms,
// 0x36), keeps timestamped snapshots, classifies freshness (ToF 300 ms class,
// AS5600 1 s class - pre-allocated #43), tracks V1 health states and
// coordinates bus recovery (adapter manual open-drain recovery: <= 16 SCL
// pulses + STOP + Wire reinit on release, with a >= 5 s cooldown -
// obligation #14; the vendor recoverBus then sees a released bus and emits
// 0 pulses). Operational motion algorithms are out of scope (#63).
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{
namespace sensing
{

// Sensor ownership (hardware contract #73): TOF_BASE_I2C_ADDR 0x08 + id =>
// 0x09..0x0C; AS5600 0x36.
enum class SensorId : std::uint8_t
{
    TofChannelReverse = 0, // ID1, 0x09
    TofChannelForward = 1, // ID2, 0x0A (absent on the bench - NACK, #73)
    TofPalletReverse = 2,  // ID3, 0x0B (absent on the bench - NACK, #73)
    TofPalletForward = 3,  // ID4, 0x0C
    As5600 = 4,            // 0x36
    Count = 5,
};

// Typed faults (domain-level; wire mapping lives in registry #47 section 16.4,
// added when the registry is ready - V1 precedent: telemetry fault bits +
// WARN_I2C_RECOVERY, evidence index).
enum class SensorFault : std::uint8_t
{
    None = 0,
    NoAck = 1,     // device did not acknowledge (transport)
    Stale = 2,     // freshness lost (age >= budget)
    BusStuck = 3,  // bus in an invalid state (requires recover())
};

// V1 health states (evidence index: TofHealthMonitor/As5600HealthMonitor).
enum class HealthState : std::uint8_t
{
    Starting = 0,
    Healthy = 1,
    Degraded = 2,
    Faulted = 3,
    Recovering = 4,
};

// Timestamped snapshot: last successful sample + age + state. Fixed-width,
// no allocation (R1); read by consumers (Safety Authority #71, Observability
// #72) in the foreground.
struct SensorSnapshot
{
    std::uint32_t raw = 0;            // ToF: distance mm (dis @0x24); AS5600: RAW ANGLE @0x0C
    std::uint32_t raw2 = 0;           // ToF: signal_strength @0x2A; AS5600: ANGLE @0x0E
    std::uint64_t sample_ms = 0;      // monotonic moment of the successful sample
    bool has_sample = false;          // a successful sample exists (sample_ms=0 is legal)
    std::uint32_t age_ms = 0xFFFFFFFFu; // now - sample_ms (0xFFFFFFFF if never)
    HealthState state = HealthState::Starting;
    SensorFault fault = SensorFault::None;
    std::uint8_t consecutive_failures = 0;
    std::uint8_t consecutive_successes = 0;
    std::uint8_t last_status = 0;     // I2cResult of the last attempt
};

// Budgets (issue #48 sections 2/5, pre-allocated #43): NOT re-designed here.
struct SensingConfig
{
    std::uint32_t tof_slot_ms = 8;             // one ToF read per slot, RR over 4
    std::uint32_t as5600_service_ms = 250;     // AS5600 service cadence
    std::uint32_t tof_fresh_ms = 300;          // T_fresh class (O3 #45)
    std::uint32_t as5600_fresh_ms = 1000;      // stale budget (pre-allocated #43)
    std::uint8_t fault_threshold = 3;          // consecutive failures -> Faulted (V1)
    std::uint8_t recovery_successes = 3;       // consecutive successes -> Healthy (V1)
    std::uint32_t recovery_cooldown_ms = 5000; // between recover() calls (#48 section 7)
};

// ToF measurement block (V1 TOF_Sense.cpp): 0x20..0x2C, little-endian.
constexpr std::uint8_t TofBaseAddr = 0x08u;
constexpr std::uint8_t TofMeasStart = 0x20u;
constexpr std::uint8_t TofMeasSize = 13u;
// AS5600: RAW ANGLE @0x0C/0x0D and ANGLE @0x0E/0x0F, 12-bit BIG-endian
// (owner contract, prototype #63) - le16() must NOT be used (byte-swap
// corrupts the angle).
constexpr std::uint8_t As5600Addr = 0x36u;
constexpr std::uint8_t As5600RawAngleReg = 0x0Cu;
constexpr std::uint8_t As5600AngleReg = 0x0Eu;

class SensingService
{
  public:
    // Startup (foreground, after kernel::init): clears snapshots/schedule.
    void init(const SensingConfig& cfg, I2cPort& i2c);

    // Bounded step (called by the composition root from a kernel step; now =
    // monotonic). One ToF slot (RR) + AS5600 when its period is due; updates
    // snapshots, freshness and recovery coordination. Never blocks longer
    // than T_step.
    void step(std::uint64_t now);

    // Period until the next slot (for re-scheduling): tof_slot_ms.
    std::uint32_t next_step_ms() const { return m_cfg.tof_slot_ms; }

    // Snapshot query (foreground). Returns false for an out-of-range id.
    bool get_snapshot(SensorId id, SensorSnapshot* out) const;

    // Total recover() calls (observable, L4 evidence).
    std::uint32_t recovery_count() const { return m_recovery_count; }

  private:
    void record_success(SensorId id, std::uint64_t now, std::uint32_t raw, std::uint32_t raw2);
    void record_failure(SensorId id, std::uint64_t now, SensorFault fault,
                        std::uint8_t status);
    void schedule_recovery(std::uint64_t now);
    void refresh_freshness(std::uint64_t now);

    I2cPort* m_i2c = nullptr;
    SensingConfig m_cfg{};
    SensorSnapshot m_snapshots[static_cast<std::uint32_t>(SensorId::Count)] = {};
    std::uint8_t m_rr_index = 0;        // ToF round-robin position (0..3)
    std::uint64_t m_next_as5600_ms = 0; // next AS5600 service due (monotonic)
    std::uint64_t m_last_recovery_ms = 0;
    std::uint32_t m_recovery_count = 0;
    bool m_initialized = false;
};

} // namespace sensing
} // namespace v3
