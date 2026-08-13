// Sensing Service implementation (design docs/sensing-slice-design-v3.md
// sections 3, 6; ticket #63). Framework-free, host-deterministic. The
// composition root passes `now` (monotonic) into step(); the service never
// reads the clock itself.
#include "domain/sensing.h"

namespace v3
{
namespace sensing
{
namespace
{

std::uint32_t le32(const std::uint8_t* b)
{
    return (static_cast<std::uint32_t>(b[3]) << 24) | (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[1]) << 8) | static_cast<std::uint32_t>(b[0]);
}

std::uint16_t le16(const std::uint8_t* b)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[1]) << 8) | b[0]);
}

// 12-bit big-endian AS5600 word (RAW ANGLE @0x0C, ANGLE @0x0E).
std::uint32_t as5600_word(const std::uint8_t* b)
{
    return static_cast<std::uint32_t>((static_cast<std::uint16_t>(b[0]) << 8) | b[1]) & 0x0FFFu;
}

} // namespace

void SensingService::init(const SensingConfig& cfg, I2cPort& i2c)
{
    m_i2c = &i2c;
    m_cfg = cfg;
    m_rr_index = 0;
    m_next_as5600_ms = 0;
    m_last_recovery_ms = 0;
    m_recovery_count = 0;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(SensorId::Count); ++i)
    {
        m_snapshots[i] = SensorSnapshot{};
    }
    m_initialized = true;
}

void SensingService::step(std::uint64_t now)
{
    if (!m_initialized || m_i2c == nullptr)
    {
        return;
    }

    // ToF round-robin: one bounded slot, next device.
    const std::uint8_t idx = m_rr_index % 4U;
    ++m_rr_index;

    std::uint8_t buf[TofMeasSize] = {};
    const I2cResult r = m_i2c->read(static_cast<std::uint8_t>(TofBaseAddr + idx + 1U),
                                    TofMeasStart, buf, TofMeasSize);
    if (r == I2cResult::Ok)
    {
        // 0x20..0x2C little-endian: dis u32 @0x24, signal u16 @0x2A.
        record_success(static_cast<SensorId>(idx), now, le32(buf + 4U), le16(buf + 10U));
    }
    else if (r == I2cResult::Stuck)
    {
        // Bus in an invalid state (HAL BUSY/TIMEOUT/ERROR): recover candidate.
        record_failure(static_cast<SensorId>(idx), now, SensorFault::BusStuck);
        schedule_recovery(now);
    }
    else if (r == I2cResult::Busy)
    {
        // BMS TX window / radio audit (Phase 2): slot skipped, no state change
        // (design T15); the cadence resumes on the next Ok.
    }
    else if (r == I2cResult::NoAck || r == I2cResult::Short)
    {
        // Device absent/unpowered or under-read (V1 TOF_Sense): transport
        // failure WITHOUT bus recovery (recovery is for stuck buses only).
        record_failure(static_cast<SensorId>(idx), now, SensorFault::NoAck);
    }
    else
    {
        // Recovered is a recover() outcome, not a read() one; treat
        // defensively as a transport failure without recovery escalation.
        record_failure(static_cast<SensorId>(idx), now, SensorFault::NoAck);
    }

    // AS5600: 250 ms service (period tracked internally; the 8 ms slot is the
    // execution granularity, jitter <= 8 ms stays far below the 1 s budget).
    if (now >= m_next_as5600_ms)
    {
        m_next_as5600_ms = now + m_cfg.as5600_service_ms;
        std::uint8_t b2[2] = {};
        const I2cResult ra = m_i2c->read(As5600Addr, As5600RawAngleReg, b2, 2U);
        std::uint8_t b3[2] = {};
        const I2cResult rb = m_i2c->read(As5600Addr, As5600AngleReg, b3, 2U);
        if (ra == I2cResult::Ok)
        {
            record_success(SensorId::As5600, now, as5600_word(b2), as5600_word(b3));
        }
        else if (ra == I2cResult::Stuck)
        {
            record_failure(SensorId::As5600, now, SensorFault::BusStuck);
            schedule_recovery(now);
        }
        else if (ra == I2cResult::Busy)
        {
            // Skip; next service due stays scheduled.
        }
        else
        {
            // NoAck/Short/Recovered: transport failure without bus recovery.
            record_failure(SensorId::As5600, now, SensorFault::NoAck);
        }
    }

    refresh_freshness(now);
}

bool SensingService::get_snapshot(SensorId id, SensorSnapshot* out) const
{
    const std::uint32_t i = static_cast<std::uint32_t>(id);
    if (out == nullptr || i >= static_cast<std::uint32_t>(SensorId::Count))
    {
        return false;
    }
    *out = m_snapshots[i];
    return true;
}

void SensingService::record_success(SensorId id, std::uint64_t now, std::uint32_t raw, std::uint32_t raw2)
{
    SensorSnapshot& s = m_snapshots[static_cast<std::uint32_t>(id)];
    s.raw = raw;
    s.raw2 = raw2;
    s.sample_ms = now;
    s.has_sample = true;
    s.fault = SensorFault::None;
    s.last_status = static_cast<std::uint8_t>(I2cResult::Ok);
    s.consecutive_failures = 0;
    if (s.consecutive_successes < 0xFFu)
    {
        ++s.consecutive_successes;
    }
    if (s.state == HealthState::Faulted || s.state == HealthState::Recovering)
    {
        s.state = (s.consecutive_successes >= m_cfg.recovery_successes)
                      ? HealthState::Healthy
                      : HealthState::Recovering;
    }
    else
    {
        s.state = HealthState::Healthy;
    }
}

void SensingService::record_failure(SensorId id, std::uint64_t now, SensorFault fault)
{
    SensorSnapshot& s = m_snapshots[static_cast<std::uint32_t>(id)];
    s.fault = fault;
    s.consecutive_successes = 0;
    if (s.consecutive_failures < 0xFFu)
    {
        ++s.consecutive_failures;
    }
    const bool was_faulted = (s.state == HealthState::Faulted || s.state == HealthState::Recovering);
    if (was_faulted || s.consecutive_failures >= m_cfg.fault_threshold)
    {
        s.state = HealthState::Faulted;
    }
    else if (s.has_sample)
    {
        s.state = HealthState::Degraded;
    }
    else
    {
        s.state = HealthState::Starting;
    }
    (void)now; // age is computed on demand by refresh_freshness
}

void SensingService::schedule_recovery(std::uint64_t now)
{
    // Cooldown >= 5 s BETWEEN attempts (issue #48 section 7): the first
    // attempt is always allowed (m_recovery_count == 0); afterwards the
    // cooldown is enforced from m_last_recovery_ms.
    if (m_recovery_count > 0u)
    {
        const std::uint64_t since = now >= m_last_recovery_ms ? now - m_last_recovery_ms : 0u;
        if (since < m_cfg.recovery_cooldown_ms)
        {
            return;
        }
    }
    m_last_recovery_ms = now;
    ++m_recovery_count;
    (void)m_i2c->recover(); // reinit + <=16 SCL pulses (adapter); per-sensor
                            // health recovers through success streaks; the
                            // WARN_I2C_RECOVERY wire code comes with #47.
}

void SensingService::refresh_freshness(std::uint64_t now)
{
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(SensorId::Count); ++i)
    {
        SensorSnapshot& s = m_snapshots[i];
        if (!s.has_sample)
        {
            s.age_ms = 0xFFFFFFFFu;
            continue;
        }
        const std::uint32_t fresh_ms = (i == static_cast<std::uint32_t>(SensorId::As5600))
                                           ? m_cfg.as5600_fresh_ms
                                           : m_cfg.tof_fresh_ms;
        const std::uint32_t age = now >= s.sample_ms ? static_cast<std::uint32_t>(now - s.sample_ms) : 0u;
        s.age_ms = age;
        if (age >= fresh_ms)
        {
            // Freshness lost (INV-SENSING-FRESH mechanism; the decision lives
            // in Safety Authority #71): stale -> Degraded, stale with the
            // failure threshold -> Faulted + Stale (V1 shouldDeclareFault).
            if (s.consecutive_failures >= m_cfg.fault_threshold)
            {
                s.state = HealthState::Faulted;
                s.fault = SensorFault::Stale;
            }
            else
            {
                s.state = HealthState::Degraded;
            }
        }
    }
}

} // namespace sensing
} // namespace v3
