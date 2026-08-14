// Sensing acquisition prototype (ticket #63, bench/sensing-proto/).
// THROWAWAY prototype - NOT production code. Answers the design questions
// that block the sensing-slice design doc:
//   1. Actual ToF read contract on the bench: contiguous block 0x20..0x2C
//      (13 B, little-endian: system_time u32, dis u32, dis_status u16,
//      signal_strength u16, range_precision u8) on 0x09..0x0C.
//   2. Actual AS5600 register contract: RAW ANGLE @0x0C/0x0D and ANGLE
//      @0x0E/0x0F are both valid 12-bit big-endian words; both are captured
//      as independent observations, the owner rotates the magnet and the
//      diff shows which one tracks.
//   3. Acquisition cadence (issue #48 section 5, V1 keep): ToF one 8 ms
//      slot per read, round-robin over 4 sensors; AS5600 every 250 ms.
//   4. One ToF read duration (budget: slot 8 ms < step 10 ms).
//   5. Fresh/Stale/Fault classification on the real bench state:
//      0x09/0x0C present -> Healthy, 0x0A/0x0B absent -> NACK -> Faulted
//      (bench record docs/l4-sensor-bench-v3.md, 2026-08-13).
//
// The diagnostic struct is pinned at 0x20011000 (linker section
// .bram_sensing, set by the sensing-proto env), stripped from the flash
// image by flash_sensing.py (runtime RAM only, same pattern as bring-up
// #61), and read back over OpenOCD by check_sensing.py. UART is disabled in
// the frozen baseline, so diagnostics go through RAM, not the UART.
#include <Arduino.h>
#include <Wire.h>

#include <cstdint>

namespace
{

constexpr std::uint32_t kMagic = 0x53454E53u; // "SENS"
constexpr std::uint32_t kVersion = 1u;

// Cadences (issue #48 section 5, V1 keep).
constexpr std::uint32_t kTofSlotMs = 8u;        // one ToF read per slot
constexpr std::uint32_t kAs5600ServiceMs = 250u;

// Freshness budgets (issue #48 section 2, pre-allocated #43): ToF 300 ms
// class, AS5600 1 s class.
constexpr std::uint32_t kTofFreshMs = 300u;
constexpr std::uint32_t kAs5600FreshMs = 1000u;

// V1 health thresholds (evidence index: TofHealthMonitor/As5600HealthMonitor):
// fault after 3 consecutive failures, recovery after 3 consecutive successes.
constexpr std::uint32_t kFaultThreshold = 3u;
constexpr std::uint32_t kRecoverySuccesses = 3u;

constexpr std::uint8_t kTofBaseAddr = 0x08u;  // + id (1..4) => 0x09..0x0C
constexpr std::uint8_t kTofMeasStart = 0x20u; // system_time @0x20
constexpr std::uint8_t kTofMeasSize = 13u;    // 0x20..0x2C inclusive
constexpr std::uint8_t kAs5600Addr = 0x36u;
constexpr std::uint8_t kAs5600RawAngleReg = 0x0Cu; // RAW ANGLE, 12-bit big-endian word
constexpr std::uint8_t kAs5600AngleReg = 0x0Eu;    // ANGLE (processed), 12-bit big-endian word

// I2C status mapping (V1 TOF_Sense.cpp classification).
constexpr std::uint8_t kStatusOk = 0u;
constexpr std::uint8_t kStatusNoAck = 1u;
constexpr std::uint8_t kStatusShort = 2u;
constexpr std::uint8_t kStatusUnknown = 3u;

// HealthState mirrors the V1 monitor states (evidence index).
enum class HealthState : std::uint8_t
{
    Starting = 0,
    Healthy = 1,
    Degraded = 2,
    Faulted = 3,
    Recovering = 4,
};

// Index 0..3 = ToF ID 1..4, index 4 = AS5600.
constexpr std::uint32_t kSensorCount = 5u;

struct SensorDiag
{    std::uint32_t raw;           // ToF: distance mm (dis @0x24); AS5600: RAW ANGLE @0x0C
    std::uint32_t raw2;          // ToF: signal_strength @0x2A; AS5600: ANGLE @0x0E (processed)
    std::uint32_t age_ms;        // age of last successful sample (0xFFFFFFFF if none)
    std::uint32_t state;         // HealthState
    std::uint32_t samples_ok;    // lifetime successful samples
    std::uint32_t samples_fail;  // lifetime failed read attempts
    std::uint32_t last_status;   // I2C status of the last read attempt
    std::uint32_t last_sample_ms;
};

struct SensingProtoDiag
{
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t uptime_ms;
    std::uint32_t loop_count;
    std::uint32_t i2c_reads;        // successful read transactions
    std::uint32_t i2c_fails;        // failed read transactions
    std::uint32_t last_tof_slot_ms; // last ToF slot (8 ms cadence)
    std::uint32_t last_as5600_ms;   // last AS5600 service (250 ms cadence)
    std::uint32_t tof_read_us_max;  // measured one-read duration, us
    std::uint32_t tof_read_us_total;
    std::uint32_t tof_read_count;
    std::uint32_t reserved;
    SensorDiag sensors[kSensorCount];
};

// Pinned at 0x20011000 by the sensing-proto env linker flag; stripped from
// the flash image by flash_sensing.py (runtime RAM only).
__attribute__((section(".bram_sensing"))) volatile SensingProtoDiag g_diag;

std::uint8_t g_fail_counts[kSensorCount] = {};
std::uint8_t g_ok_counts[kSensorCount] = {};
std::uint8_t g_tof_index = 0; // round-robin position (0..3 -> ToF id 1..4)
std::uint32_t g_last_tof_slot = 0;
std::uint32_t g_last_as5600 = 0;

// I2C wiring variants (diagnostic): 0 = PB11/PB10 (I2C2, netlist), 1 =
// PB7/PB6 (I2C1, alternate F405 pair). The probe auto-switches to variant 1
// after 2 s without a single successful read, so one run covers both wiring
// hypotheses. word 11 of the diagnostic header mirrors the active variant.
std::uint8_t g_wire_variant = 0;

std::uint32_t le32(const std::uint8_t* b)
{
    return (static_cast<std::uint32_t>(b[3]) << 24) | (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[1]) << 8) | static_cast<std::uint32_t>(b[0]);
}

std::uint16_t le16(const std::uint8_t* b)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[1]) << 8) | b[0]);
}

// V1-style I2C read with status classification (TOF_Sense.cpp). Both
// transactions finish with STOP; STM32duino collapses every read-phase
// failure to zero (TOF_I2C_READ_FAILED_UNKNOWN -> kStatusUnknown).
std::uint8_t i2c_read(std::uint8_t addr, std::uint8_t reg, std::uint8_t* out, std::uint8_t len)
{
    Wire.beginTransmission(addr);
    if (Wire.write(reg) != 1U)
    {
        (void)Wire.endTransmission(true);
        return kStatusUnknown;
    }
    const std::uint8_t tx = Wire.endTransmission(true);
    if (tx != 0U)
    {
        return (tx == 2U || tx == 3U) ? kStatusNoAck : kStatusUnknown;
    }
    const std::uint8_t requested = Wire.requestFrom(addr, len);
    std::uint8_t received = 0U;
    while (Wire.available() > 0 && received < len)
    {
        out[received++] = static_cast<std::uint8_t>(Wire.read());
    }
    while (Wire.available() > 0)
    {
        (void)Wire.read();
    }
    if (requested == 0U)
    {
        return kStatusUnknown;
    }
    if (requested != len || received != len)
    {
        return kStatusShort;
    }
    return kStatusOk;
}

void record_result(std::uint32_t idx, std::uint32_t now, std::uint32_t raw, std::uint32_t raw2, std::uint8_t status)
{
    volatile SensorDiag& s = g_diag.sensors[idx];
    s.last_status = status;
    if (status == kStatusOk)
    {
        s.raw = raw;
        s.raw2 = raw2;
        s.last_sample_ms = now;
        ++s.samples_ok;
        ++g_diag.i2c_reads;
        g_fail_counts[idx] = 0U;
        ++g_ok_counts[idx];
        if (g_ok_counts[idx] >= kRecoverySuccesses)
        {
            s.state = static_cast<std::uint32_t>(HealthState::Healthy);
        }
        else
        {
            s.state = static_cast<std::uint32_t>(HealthState::Recovering);
        }
    }
    else
    {
        ++s.samples_fail;
        ++g_diag.i2c_fails;
        ++g_fail_counts[idx];
        g_ok_counts[idx] = 0U;
        if (g_fail_counts[idx] >= kFaultThreshold)
        {
            s.state = static_cast<std::uint32_t>(HealthState::Faulted);
        }
        else if (s.samples_ok > 0U)
        {
            s.state = static_cast<std::uint32_t>(HealthState::Degraded);
        }
        else
        {
            s.state = static_cast<std::uint32_t>(HealthState::Starting);
        }
    }
}

void read_tof(std::uint32_t idx)
{
    const std::uint8_t addr = static_cast<std::uint8_t>(kTofBaseAddr + idx + 1U);
    std::uint8_t buf[kTofMeasSize] = {};
    const std::uint32_t t0 = micros();
    const std::uint8_t status = i2c_read(addr, kTofMeasStart, buf, kTofMeasSize);
    const std::uint32_t dt = micros() - t0;
    if (dt > g_diag.tof_read_us_max)
    {
        g_diag.tof_read_us_max = dt;
    }
    g_diag.tof_read_us_total += dt;
    ++g_diag.tof_read_count;

    std::uint32_t raw = 0U;
    std::uint32_t raw2 = 0U;
    if (status == kStatusOk)
    {
        raw = le32(buf + 4U);   // dis @0x24
        raw2 = le16(buf + 10U); // signal_strength @0x2A
    }
    record_result(idx, millis(), raw, raw2, status);
}

void read_as5600()
{
    // RAW ANGLE @0x0C/0x0D and ANGLE @0x0E/0x0F are both valid 12-bit
    // big-endian words on this part (owner contract): value = (b[0] << 8) |
    // b[1] & 0x0FFF. le16() must NOT be used here - it would byte-swap the
    // word and corrupt the observed angle. Both are captured as independent
    // observations; the owner rotates the magnet during the live run and the
    // diff shows which register tracks rotation.
    std::uint8_t buf[2] = {};
    const std::uint8_t status_raw = i2c_read(kAs5600Addr, kAs5600RawAngleReg, buf, 2U);
    std::uint32_t raw = 0U;
    if (status_raw == kStatusOk)
    {
        raw = static_cast<std::uint32_t>((static_cast<std::uint16_t>(buf[0]) << 8) | buf[1]) & 0x0FFFU;
    }
    std::uint8_t buf2[2] = {};
    const std::uint8_t status_ang = i2c_read(kAs5600Addr, kAs5600AngleReg, buf2, 2U);
    std::uint32_t raw2 = 0U;
    if (status_ang == kStatusOk)
    {
        raw2 = static_cast<std::uint32_t>((static_cast<std::uint16_t>(buf2[0]) << 8) | buf2[1]) & 0x0FFFU;
    }
    // Angle availability is the operational requirement (V1 AS5600Sensor:
    // optional diagnostics do not fault the sensor). Primary status = raw reg.
    const std::uint8_t status = (status_raw == kStatusOk) ? kStatusOk : status_raw;
    record_result(4U, millis(), raw, raw2, status);
}

void refresh_health(std::uint32_t now)
{
    for (std::uint32_t i = 0; i < kSensorCount; ++i)
    {
        volatile SensorDiag& s = g_diag.sensors[i];
        const std::uint32_t fresh_ms = (i == 4U) ? kAs5600FreshMs : kTofFreshMs;
        if (s.samples_ok == 0U)
        {
            s.state = static_cast<std::uint32_t>(HealthState::Starting);
            s.age_ms = 0xFFFFFFFFu;
            continue;
        }
        const std::uint32_t age = now >= s.last_sample_ms ? now - s.last_sample_ms : 0U;
        s.age_ms = age;
        if (age >= fresh_ms && g_fail_counts[i] >= kFaultThreshold)
        {
            // Fresh lost while already faulted: stays faulted (stale-fault).
            s.state = static_cast<std::uint32_t>(HealthState::Faulted);
        }
    }
}

} // namespace

void setup()
{
    // Zero the diagnostic RAM first: a stale marker from an interrupted run
    // must never be mistaken for a live snapshot (flash_sensing.py also
    // zeroes the address while halted before reset run).
    volatile std::uint32_t* p = reinterpret_cast<volatile std::uint32_t*>(&g_diag);
    const volatile std::uint32_t* end =
        p + (sizeof(SensingProtoDiag) / sizeof(std::uint32_t));
    for (; p < end; ++p)
    {
        *p = 0U;
    }
    g_diag.magic = kMagic;
    g_diag.version = kVersion;

    // I2C init in the WORKING V1 style (Cntrl_V2 initTofI2cBus, proven on the
    // bench 2026-08-13): setSDA/setSCL + begin() + setClock. The earlier
    // prototype used Wire.begin(PB11, PB10) which produced tx=4 (Stuck) on
    // the same bus - this run checks whether the init call style matters.
    Wire.setSDA(PB11);
    Wire.setSCL(PB10);
    Wire.begin();
    Wire.setClock(100000UL);
}

void loop()
{
    const std::uint32_t now = millis();

    // Diagnostic auto-switch is DISABLED in this run (V1-style init probe):
    // the bus stays on PB11/PB10; g_wire_variant stays 0 so the header
    // reports which init style was active.
    if (false && g_wire_variant == 0U && now > 2000U && g_diag.i2c_reads == 0U)
    {
        Wire.end();
        Wire.begin(PB7, PB6);
        Wire.setClock(100000UL);
        g_wire_variant = 1U;
        g_diag.i2c_reads = 0U;
        g_diag.i2c_fails = 0U;
        for (std::uint32_t i = 0; i < kSensorCount; ++i)
        {
            g_fail_counts[i] = 0U;
            g_ok_counts[i] = 0U;
        }
    }
    g_diag.reserved = g_wire_variant;

    // ToF round-robin: one 8 ms slot per read, cycling over 4 sensors
    // (issue #48 section 5). A slot is a single bounded transaction, so a
    // full RR cycle is 4 x ~8 ms ~= 32 ms per sensor.
    if (now - g_last_tof_slot >= kTofSlotMs)
    {
        g_last_tof_slot = now;
        g_diag.last_tof_slot_ms = now;
        read_tof(g_tof_index);
        g_tof_index = static_cast<std::uint8_t>((g_tof_index + 1U) & 3U);
    }

    // AS5600: 250 ms service cadence (issue #48 section 5).
    if (now - g_last_as5600 >= kAs5600ServiceMs)
    {
        g_last_as5600 = now;
        g_diag.last_as5600_ms = now;
        read_as5600();
    }

    refresh_health(now);

    g_diag.uptime_ms = now;
    ++g_diag.loop_count;
}
