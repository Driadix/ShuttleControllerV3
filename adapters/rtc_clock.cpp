// Wall-clock adapter implementation (design docs/observability-design-v3.md
// section 4.2; ticket #72). Read-only RTC calendar -> epoch, register-level.
#include "adapters/rtc_clock.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <stm32f4xx.h>

namespace v3
{

namespace
{

// BCD byte (two decimal digits) to binary.
inline std::uint32_t bcd2bin(std::uint32_t b)
{
    return ((b >> 4) & 0x0Fu) * 10u + (b & 0x0Fu);
}

// Days since 1970-01-01 (civil-from-days inverse, Howard Hinnant's algorithm;
// bounded, deterministic, no allocation). Valid for 1970..2100 (SetWallClock
// plausibility window, #49 section 3).
std::int64_t days_from_civil(std::int64_t y, std::uint32_t m, std::uint32_t d)
{
    y -= m <= 2u ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::uint32_t yoe = static_cast<std::uint32_t>(y - era * 400);
    const std::uint32_t doy = (153u * (m + (m > 2u ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const std::uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

} // namespace

void RtcClock::init()
{
    // Read-only adapter: no RTC initialization here (belongs to #76 / RTC
    // power). The RTC calendar must already be running (V1 set the time over
    // the Service path; #75/#76 SetWallClock keeps it current).
    m_synced_this_epoch = false; // reboot resets the per-epoch synced flag (#49 s3)
}

codec::TimeValidity RtcClock::time_validity() const
{
    // INITS (calendar initialized flag): 0 => RTC never configured (dead VBAT /
    // first boot / LSE fail) => Unsynced, wall must NOT be emitted (#49 s3:
    // V1 silently reset the RTC to 2023-01-01 - the defect this design fixes).
    if ((RTC->ISR & RTC_ISR_INITS) == 0u)
    {
        return codec::TimeValidity::Unsynced;
    }
    // Calendar runs but no SetWallClock in this epoch: RTC-only quality.
    // Synced is reached only via mark_synced() (#75/#76 Service path).
    return m_synced_this_epoch ? codec::TimeValidity::Synced : codec::TimeValidity::RtcOnly;
}

std::uint32_t RtcClock::epoch_sec() const
{
    if (time_validity() == codec::TimeValidity::Unsynced)
    {
        return 0; // never emit a convincing time from an uninitialized RTC
    }
    // RTC TR/DR are BCD; wait for RSF (registers synchronized) - bounded poll.
    std::uint32_t guard = 0;
    while ((RTC->ISR & RTC_ISR_RSF) == 0u && guard++ < 1000u)
    {
        // RSF clears when TR/DR are read; short poll, no blocking beyond bound.
    }
    const std::uint32_t tr = RTC->TR;
    const std::uint32_t dr = RTC->DR;

    const std::uint32_t hh = bcd2bin((tr >> 16) & 0x3Fu);
    const std::uint32_t mm = bcd2bin((tr >> 8) & 0x7Fu);
    const std::uint32_t ss = bcd2bin(tr & 0x7Fu);
    const std::uint32_t yy = bcd2bin((dr >> 16) & 0xFFu) + 2000u; // 2-digit year + 2000
    const std::uint32_t mo = bcd2bin((dr >> 8) & 0x1Fu);
    const std::uint32_t dd = bcd2bin(dr & 0x3Fu);

    const std::int64_t days = days_from_civil(static_cast<std::int64_t>(yy), mo, dd);
    if (days < 0)
    {
        return 0; // before 1970: invalid, treat as unsynced
    }
    const std::int64_t secs = days * 86400 + static_cast<std::int64_t>(hh) * 3600 +
                              static_cast<std::int64_t>(mm) * 60 + static_cast<std::int64_t>(ss);
    return static_cast<std::uint32_t>(secs); // fits until 2106 (32-bit epoch)
}

} // namespace v3

#endif // ARDUINO
