// test_sensing suite: ToF round-robin, cadence, snapshot update, freshness
// (stale-fault), health state machine, recovery cooldown, AS5600 big-endian,
// bounded transactions per step, snapshot queries, ISR boundary, Busy skip
// (design docs/sensing-slice-design-v3.md section 7.3 T1-T15). Host,
// deterministic: a scripted FakeI2cPort drives the service, `now` is passed
// in explicitly (the domain never reads a clock).
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "domain/sensing.h"

namespace
{

using v3::sensing::HealthState;
using v3::sensing::SensorFault;
using v3::sensing::SensorId;
using v3::sensing::SensorSnapshot;
using v3::sensing::SensingService;

// Scripted I2C port (design section 7.2): a bounded FIFO of read outcomes
// (fixed-size, no std::vector - tests compile with -fno-exceptions, R1
// spirit). Records every (device, reg) for order assertions.
class FakeI2cPort : public v3::I2cPort
{
  public:
    static constexpr std::uint32_t kMaxScript = 256;
    static constexpr std::uint32_t kMaxLog = 512;

    struct Outcome
    {
        v3::I2cResult result = v3::I2cResult::Ok;
        std::uint8_t bytes[16] = {};
        std::uint8_t count = 0;
    };

    v3::I2cResult read(std::uint8_t device, std::uint8_t reg,
                       std::uint8_t* out, std::uint8_t len) override
    {
        if (m_log_count < kMaxLog)
        {
            m_log_devices[m_log_count] = device;
            m_log_regs[m_log_count] = reg;
            ++m_log_count;
        }
        // FIFO until the script is exhausted, then repeat the last entry.
        const Outcome o = (m_read_pos < m_write_pos)
                              ? m_script[m_read_pos]
                              : (m_write_pos > 0u ? m_script[m_write_pos - 1u] : Outcome{});
        if (m_read_pos < m_write_pos)
        {
            ++m_read_pos;
        }
        if (out != nullptr && o.result == v3::I2cResult::Ok)
        {
            const std::uint8_t n = o.count < len ? o.count : len;
            for (std::uint8_t i = 0; i < n; ++i)
            {
                out[i] = o.bytes[i];
            }
        }
        m_last_status = static_cast<std::uint8_t>(o.result);
        return o.result;
    }

    v3::I2cResult recover() override
    {
        ++m_recover_count;
        return v3::I2cResult::Recovered;
    }

    std::uint8_t last_wire_status() const override { return m_last_status; }

    // Script: outcomes are consumed in FIFO order; the last entry repeats.
    void queue(v3::I2cResult r, const std::uint8_t* bytes = nullptr, std::uint8_t n = 0)
    {
        if (m_write_pos < kMaxScript)
        {
            Outcome o;
            o.result = r;
            o.count = n;
            if (bytes != nullptr && n > 0 && n <= sizeof(o.bytes))
            {
                for (std::uint8_t i = 0; i < n; ++i)
                {
                    o.bytes[i] = bytes[i];
                }
            }
            m_script[m_write_pos++] = o;
        }
    }
    // Append `count` copies of the last scripted outcome (for cadence tests).
    void repeat_last(std::uint32_t count)
    {
        if (m_write_pos == 0u)
        {
            return;
        }
        const Outcome o = m_script[m_write_pos - 1u];
        for (std::uint32_t i = 0; i < count && m_write_pos < kMaxScript; ++i)
        {
            m_script[m_write_pos++] = o;
        }
    }
    void reset_script()
    {
        m_write_pos = 0;
        m_read_pos = 0;
    }

    std::uint32_t recover_count() const { return m_recover_count; }
    std::uint32_t log_count() const { return m_log_count; }
    std::uint8_t log_device(std::uint32_t i) const { return m_log_devices[i]; }
    std::uint8_t log_reg(std::uint32_t i) const { return m_log_regs[i]; }

  private:
    Outcome m_script[kMaxScript] = {};
    std::uint32_t m_write_pos = 0;
    std::uint32_t m_read_pos = 0;
    std::uint32_t m_recover_count = 0;
    std::uint32_t m_log_count = 0;
    std::uint8_t m_log_devices[kMaxLog] = {};
    std::uint8_t m_log_regs[kMaxLog] = {};
    std::uint8_t m_last_status = 0;
};

// ToF measurement block bytes for a 1500 mm / signal 100 sample (0x20..0x2C:
// system_time u32, dis u32 @0x24, status u16 @0x28, signal u16 @0x2A,
// precision u8 @0x2C; little-endian).
void tof_block(std::uint8_t out[16], std::uint32_t dis = 1500u, std::uint16_t signal = 100u)
{
    for (std::uint8_t i = 0; i < 16; ++i)
    {
        out[i] = 0;
    }
    out[4] = static_cast<std::uint8_t>(dis & 0xFFu);          // dis @0x24 LE
    out[5] = static_cast<std::uint8_t>((dis >> 8) & 0xFFu);
    out[6] = static_cast<std::uint8_t>((dis >> 16) & 0xFFu);
    out[7] = static_cast<std::uint8_t>((dis >> 24) & 0xFFu);
    out[8] = 1u;                                               // dis_status = valid
    out[10] = static_cast<std::uint8_t>(signal & 0xFFu);       // signal @0x2A LE
    out[11] = static_cast<std::uint8_t>((signal >> 8) & 0xFFu);
}

class SensingTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_svc.init(m_cfg, m_i2c);
    }

    FakeI2cPort m_i2c;
    v3::sensing::SensingConfig m_cfg{};
    SensingService m_svc;
};

// T1: ToF round-robin - 4 slots read all 4 IDs in order (8 ms each). The
// first slot also services AS5600 (startup), so the log is filtered for ToF.
TEST_F(SensingTest, TofRoundRobinOrder)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    for (std::uint32_t i = 0; i < 4; ++i)
    {
        m_i2c.queue(v3::I2cResult::Ok, b, 13);
    }
    m_i2c.repeat_last(4);
    for (std::uint64_t t = 0; t < 32; t += 8)
    {
        m_svc.step(t);
    }
    std::uint8_t tof_seen[4] = {};
    std::uint8_t n = 0;
    for (std::uint32_t i = 0; i < m_i2c.log_count() && n < 4u; ++i)
    {
        const std::uint8_t dev = m_i2c.log_device(i);
        if (dev != v3::sensing::As5600Addr)
        {
            tof_seen[n++] = dev;
        }
    }
    ASSERT_EQ(n, 4u);
    EXPECT_EQ(tof_seen[0], 0x09u);
    EXPECT_EQ(tof_seen[1], 0x0Au);
    EXPECT_EQ(tof_seen[2], 0x0Bu);
    EXPECT_EQ(tof_seen[3], 0x0Cu);
    EXPECT_EQ(m_i2c.log_reg(0), 0x20u);
}

// T2: cadence - 100 ToF slots -> ~25 reads per sensor; AS5600 serviced at
// 250 ms (4 services over 800 ms) with its own reads.
TEST_F(SensingTest, Cadence)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    std::uint8_t ang[2] = {0x12, 0x34};
    // Script: alternate ToF Ok and AS5600 Ok (both succeed throughout).
    for (std::uint32_t i = 0; i < 100; ++i)
    {
        m_i2c.queue(v3::I2cResult::Ok, b, 13);
    }
    m_i2c.repeat_last(1000); // AS5600 reads reuse the ToF entry (Ok)
    m_i2c.queue(v3::I2cResult::Ok, ang, 2);

    for (std::uint32_t i = 0; i < 100; ++i)
    {
        m_svc.step(static_cast<std::uint64_t>(i) * 8u);
    }
    SensorSnapshot s;
    // 100 slots / 4 sensors = 25 reads per sensor; all present sensors
    // succeeded (scripted Ok), so each has 25 consecutive successes.
    for (std::uint32_t id = 0; id < 4; ++id)
    {
        ASSERT_TRUE(m_svc.get_snapshot(static_cast<SensorId>(id), &s));
        EXPECT_EQ(s.consecutive_successes, 25u);
    }
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::As5600, &s));
    EXPECT_EQ(s.consecutive_successes, 4u); // 0, 250, 500, 750 ms
}

// T3: Ok -> snapshot updated (raw/raw2, sample_ms, Healthy). Sensor 0x09 is
// read on the first slot (t=0).
TEST_F(SensingTest, SnapshotUpdatedOnOk)
{
    std::uint8_t b[16] = {};
    tof_block(b, 1500u, 100u);
    m_i2c.queue(v3::I2cResult::Ok, b, 13);
    m_i2c.repeat_last(3); // AS5600 startup reads (Ok)
    m_svc.step(0);
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.raw, 1500u);
    EXPECT_EQ(s.raw2, 100u);
    EXPECT_EQ(s.sample_ms, 0u);
    EXPECT_TRUE(s.has_sample);
    EXPECT_EQ(s.state, HealthState::Healthy);
}

// T4: freshness - age >= T_fresh (300 ms) degrades; threshold failures while
// stale -> Faulted + Stale. Round-robin advances per step(): sensor 0x09 is
// read on step calls 1, 5, 9, 13, ... (every 32 ms at 8 ms cadence).
TEST_F(SensingTest, FreshnessStaleFault)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // step 1: 0x09 ok
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // step 1: as5600 RAW (startup)
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // step 1: as5600 ANGLE
    m_i2c.queue(v3::I2cResult::NoAck);
    m_i2c.repeat_last(256);                 // all later reads fail

    for (std::uint64_t t = 0; t <= 400; t += 8)
    {
        m_svc.step(t);
    }
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Faulted); // fails on steps 5, 9, 13
    EXPECT_EQ(s.fault, SensorFault::Stale);   // age 400 >= 300 while faulted
    EXPECT_EQ(s.age_ms, 400u);

    // Intermediate: after step 5 (t=32, fail 1) the sensor degrades.
    m_svc.init(m_cfg, m_i2c);
    m_i2c.reset_script();
    m_i2c.queue(v3::I2cResult::Ok, b, 13);
    m_i2c.queue(v3::I2cResult::Ok, b, 13);
    m_i2c.queue(v3::I2cResult::Ok, b, 13);
    m_i2c.queue(v3::I2cResult::NoAck);
    m_i2c.repeat_last(16);
    for (std::uint64_t t = 0; t <= 32; t += 8)
    {
        m_svc.step(t); // steps 1..5
    }
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Degraded); // 1 failure, still < threshold
}

// T5: AS5600 stale 1 s -> Faulted + Stale. Read sequence per step: 1 ToF +
// 2 AS5600 registers when the service is due (0, 250, 500, 750, 1000 ms).
TEST_F(SensingTest, As5600StaleFault)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    std::uint8_t ang[2] = {0x12, 0x34};
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=0   tof
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // t=0   as5600 RAW
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // t=0   as5600 ANGLE
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=250 tof
    m_i2c.queue(v3::I2cResult::NoAck);      // t=250 as5600 RAW (fail 1)
    m_i2c.queue(v3::I2cResult::NoAck);      // t=250 as5600 ANGLE
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=500 tof
    m_i2c.queue(v3::I2cResult::NoAck);      // t=500 as5600 RAW (fail 2)
    m_i2c.queue(v3::I2cResult::NoAck);      // t=500 as5600 ANGLE
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=750 tof
    m_i2c.queue(v3::I2cResult::NoAck);      // t=750 as5600 RAW (fail 3 -> Faulted)
    m_i2c.queue(v3::I2cResult::NoAck);      // t=750 as5600 ANGLE
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=1000 tof
    m_i2c.queue(v3::I2cResult::NoAck);      // t=1000 as5600 RAW (fail 4)
    m_i2c.queue(v3::I2cResult::NoAck);      // t=1000 as5600 ANGLE

    m_svc.step(0);
    m_svc.step(250);
    m_svc.step(500);
    m_svc.step(750);
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::As5600, &s));
    EXPECT_EQ(s.state, HealthState::Faulted);
    EXPECT_EQ(s.consecutive_failures, 3u);
    // Age from the last success (t=0) at t=1000 must cross the 1 s budget.
    m_svc.step(1000);
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::As5600, &s));
    EXPECT_EQ(s.age_ms, 1000u);
    EXPECT_EQ(s.state, HealthState::Faulted);
    EXPECT_EQ(s.fault, SensorFault::Stale);
}

// T6: NoAck x3 -> Faulted; 2 failures -> Degraded (V1 threshold). Sensor
// 0x09 is read on step calls 1, 5, 9, 13 (8 ms cadence).
TEST_F(SensingTest, NoAckThreshold)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    m_i2c.queue(v3::I2cResult::Ok, b, 13); // step 1: 0x09 ok
    m_i2c.queue(v3::I2cResult::Ok, b, 13); // step 1: as5600 RAW
    m_i2c.queue(v3::I2cResult::Ok, b, 13); // step 1: as5600 ANGLE
    m_i2c.queue(v3::I2cResult::NoAck);
    m_i2c.repeat_last(32);

    for (std::uint64_t t = 0; t <= 64; t += 8)
    {
        m_svc.step(t); // steps 1..9: 0x09 fails on 5 and 9
    }
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Degraded);
    EXPECT_EQ(s.consecutive_failures, 2u);

    for (std::uint64_t t = 72; t <= 96; t += 8)
    {
        m_svc.step(t); // step 13: 0x09 fail 3
    }
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Faulted);
    EXPECT_EQ(s.consecutive_failures, 3u);
}

// T7: recovery - Faulted -> 3 consecutive successes -> Healthy (via
// Recovering). Read indices (step 1 consumes 3 reads: 0x09 + as5600 x2;
// every later step consumes 1): 0x09 at reads 0, 6, 10, 14 (fail x3), 18,
// 22, 26 (ok x3). AS5600 service is pushed far out (startup reads only).
TEST_F(SensingTest, RecoveryPath)
{
    m_cfg.as5600_service_ms = 1000000u;
    m_svc.init(m_cfg, m_i2c);
    std::uint8_t b[16] = {};
    tof_block(b);
    std::uint8_t ang[2] = {0x00, 0x00};
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 0:  0x09 ok
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // read 1:  as5600 RAW (startup)
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // read 2:  as5600 ANGLE
    m_i2c.queue(v3::I2cResult::NoAck);      // read 3:  0x0A
    m_i2c.repeat_last(14);                  // reads 4..17: fail (0x09 6,10,14)
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 18: 0x09 ok -> Recovering (1)
    m_i2c.queue(v3::I2cResult::NoAck);      // read 19: 0x0A
    m_i2c.repeat_last(2);                   // reads 20,21
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 22: 0x09 ok -> Recovering (2)
    m_i2c.queue(v3::I2cResult::NoAck);      // read 23: 0x0B
    m_i2c.repeat_last(2);                   // reads 24,25
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 26: 0x09 ok -> Healthy (3)

    for (std::uint64_t t = 0; t <= 192; t += 8)
    {
        m_svc.step(t); // steps 1..25
    }
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Healthy);
    EXPECT_EQ(s.consecutive_successes, 3u);
    EXPECT_EQ(s.fault, SensorFault::None);
}

// T7a: intermediate transitions - Degraded at fail 1, Faulted at fail 3,
// Recovering after the first success.
TEST_F(SensingTest, RecoveryIntermediateStates)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    std::uint8_t ang[2] = {0x00, 0x00};
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 0:  0x09 ok
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // read 1,2: as5600 startup
    m_i2c.queue(v3::I2cResult::Ok, ang, 2);
    m_i2c.queue(v3::I2cResult::NoAck);      // read 3:  0x0A
    m_i2c.repeat_last(11);                  // reads 4..14 fail (0x09 6,10,14)
    m_i2c.queue(v3::I2cResult::NoAck);      // read 15: 0x0A
    m_i2c.repeat_last(2);                   // reads 16,17
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 18: 0x09 ok -> Recovering (1)

    SensorSnapshot s;
    for (std::uint64_t t = 0; t <= 32; t += 8)
    {
        m_svc.step(t); // steps 1..5: 0x09 fail 1 at read 6
    }
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Degraded);
    EXPECT_EQ(s.consecutive_failures, 1u);

    for (std::uint64_t t = 40; t <= 96; t += 8)
    {
        m_svc.step(t); // steps 6..13: fails 2 and 3 at reads 10, 14
    }
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Faulted);
    EXPECT_EQ(s.consecutive_failures, 3u);

    for (std::uint64_t t = 104; t <= 128; t += 8)
    {
        m_svc.step(t); // steps 14..17: 0x09 ok at read 18
    }
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Recovering);
    EXPECT_EQ(s.consecutive_successes, 1u);
}

// T8: Stuck -> recovery with cooldown >= 5 s between attempts. All reads are
// Stuck; sensor 0x09 (step calls 1, 5, 9, 13) accumulates 3 failures. The
// first Stuck triggers recover() (t=0); attempts within the cooldown are
// skipped; the 0x09 slot at t=5024 (step 629) triggers the second one.
TEST_F(SensingTest, RecoveryCooldown)
{
    m_i2c.queue(v3::I2cResult::Stuck); // read 0: 0x09 Stuck -> recover #1
    m_i2c.repeat_last(1024);
    m_svc.step(0);
    EXPECT_EQ(m_i2c.recover_count(), 1u);

    m_svc.step(8); // 0x0A Stuck: within cooldown -> no recover
    EXPECT_EQ(m_i2c.recover_count(), 1u);

    for (std::uint64_t t = 16; t <= 96; t += 8)
    {
        m_svc.step(t); // steps 3..13: 0x09 fails 2 and 3 at steps 9, 13
    }
    EXPECT_EQ(m_i2c.recover_count(), 1u); // cooldown still active
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Faulted);
    EXPECT_EQ(s.fault, SensorFault::BusStuck);

    m_svc.step(5024); // 0x09 Stuck: 5.024 s elapsed -> recover #2
    EXPECT_EQ(m_i2c.recover_count(), 2u);
    EXPECT_EQ(m_svc.recovery_count(), 2u);
}

// T9: cooldown boundary - exactly 5 s since the last attempt allows the next
// recover; 1 ms short does not.
TEST_F(SensingTest, RecoveryBoundary)
{
    m_i2c.queue(v3::I2cResult::Stuck); // t=0 -> #1
    m_i2c.repeat_last(512);
    m_svc.step(0);
    m_svc.step(4999); // 0x09 slot: 4.999 s -> skipped
    EXPECT_EQ(m_i2c.recover_count(), 1u);
    m_svc.step(5000); // 0x0A slot: 5.000 s exactly -> #2
    EXPECT_EQ(m_i2c.recover_count(), 2u);
}

// T10: AS5600 big-endian - bytes [0x0A,0xBC] on 0x0C -> raw 0x0ABC (le16
// would give 0x0BCA, proving the byte order); [0x0B,0xCD] on 0x0E -> raw2
// 0x0BCD.
TEST_F(SensingTest, As5600BigEndian)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    std::uint8_t raw[2] = {0x0A, 0xBC};
    std::uint8_t ang[2] = {0x0B, 0xCD};
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // tof
    m_i2c.queue(v3::I2cResult::Ok, raw, 2); // as5600 RAW ANGLE @0x0C
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // as5600 ANGLE @0x0E
    m_svc.step(0);
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::As5600, &s));
    EXPECT_EQ(s.raw, 0x0ABCu);
    EXPECT_EQ(s.raw2, 0x0BCDu);
}

// T11: bounded transactions per step - at most 3 reads (ToF + 2 AS5600
// registers when the service is due; the first step services AS5600 at
// startup, the next slots do not until 250 ms have passed).
TEST_F(SensingTest, BoundedTransactionsPerStep)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    std::uint8_t raw[2] = {0x00, 0x01};
    std::uint8_t ang[2] = {0x00, 0x02};
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=8   tof
    m_i2c.queue(v3::I2cResult::Ok, raw, 2); // t=8   as5600 RAW (startup)
    m_i2c.queue(v3::I2cResult::Ok, ang, 2); // t=8   as5600 ANGLE
    m_svc.step(8);
    EXPECT_EQ(m_i2c.log_count(), 3u);       // startup step: tof + as5600 x2

    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // t=16 tof only (no as5600 due)
    m_svc.step(16);
    EXPECT_EQ(m_i2c.log_count(), 4u);       // +1
}

// T12: next_step_ms is the ToF slot period (composition root re-schedules).
TEST_F(SensingTest, NextStepPeriod)
{
    EXPECT_EQ(m_svc.next_step_ms(), m_cfg.tof_slot_ms);
    EXPECT_EQ(m_svc.next_step_ms(), 8u);
}

// T13: snapshot queries - valid id, out-of-range id, null out.
TEST_F(SensingTest, SnapshotQueryContract)
{
    SensorSnapshot s;
    EXPECT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Starting);
    EXPECT_FALSE(m_svc.get_snapshot(SensorId::Count, &s));
    EXPECT_FALSE(m_svc.get_snapshot(SensorId::TofChannelReverse, nullptr));
}

// T14: ISR boundary / framework-free - domain sensing code must not include
// Arduino or reference the kernel; the I2C adapter must not reference the
// kernel/sensing (rule R2, #51 section 5.2; include-lint).
TEST_F(SensingTest, DomainFrameworkFree)
{
    std::ifstream in("domain/sensing.cpp");
    ASSERT_TRUE(in.is_open()) << "domain/sensing.cpp not readable from test cwd";
    std::ostringstream src;
    src << in.rdbuf();
    const std::string code = src.str();
    EXPECT_EQ(code.find("Arduino"), std::string::npos);
    EXPECT_EQ(code.find("kernel::"), std::string::npos);
    EXPECT_EQ(code.find("monotonic::"), std::string::npos);
    EXPECT_EQ(code.find("platform/"), std::string::npos);

    std::ifstream in2("adapters/i2c_bus.cpp");
    ASSERT_TRUE(in2.is_open()) << "adapters/i2c_bus.cpp not readable";
    std::ostringstream src2;
    src2 << in2.rdbuf();
    const std::string adapter = src2.str();
    EXPECT_EQ(adapter.find("kernel::"), std::string::npos);
    EXPECT_EQ(adapter.find("sensing::"), std::string::npos);
    EXPECT_EQ(adapter.find("monotonic::"), std::string::npos);
}

// T15: Busy (BMS-quiet) - slot skipped without state change; the cadence
// resumes on the next Ok. Steps 1..5 (t=0..32): 0x09 at 1 and 5, 0x0A/B/C in
// between are Busy (skipped, they do not touch 0x09).
TEST_F(SensingTest, BusySlotSkipped)
{
    std::uint8_t b[16] = {};
    tof_block(b);
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 0: 0x09 ok
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 1,2: as5600 startup
    m_i2c.queue(v3::I2cResult::Ok, b, 13);
    m_i2c.queue(v3::I2cResult::Busy);       // read 3: 0x0A busy (skipped)
    m_i2c.queue(v3::I2cResult::Busy);       // read 4: 0x0B busy
    m_i2c.queue(v3::I2cResult::Busy);       // read 5: 0x0C busy
    m_i2c.queue(v3::I2cResult::Ok, b, 13);  // read 6: 0x09 ok (resumes)
    m_svc.step(0);
    m_svc.step(8);
    SensorSnapshot s;
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.state, HealthState::Healthy);
    EXPECT_EQ(s.consecutive_failures, 0u);
    EXPECT_EQ(s.consecutive_successes, 1u); // Busy slots do not touch 0x09

    m_svc.step(16);
    m_svc.step(24);
    m_svc.step(32); // step 5: 0x09 ok again
    ASSERT_TRUE(m_svc.get_snapshot(SensorId::TofChannelReverse, &s));
    EXPECT_EQ(s.consecutive_successes, 2u);
    EXPECT_EQ(s.state, HealthState::Healthy);
}

} // namespace
