// Bring-up watchdog fire experiment (ticket #61, D2b, design
// docs/bringup-design-v3.md section 6). Proves arming + firing + reset on
// STM32F405 (the IWDG has no readable down-counter or WDGA bit - RM0090
// section 22.3 - so a behavioral experiment is the only evidence).
//
// RAM phase-marker flow (marker survives NRST and IWDG reset - RAM is not
// cleared by resets, only by power cycle):
//   boot, marker != ARMED  -> clear ALL RCC reset flags (a stale IWDGRSTF
//                             from a previous experiment must not
//                             false-pass), marker = ARMED, arm IWDG (2 s),
//                             NO reload -> watchdog fires, board resets.
//   boot, marker == ARMED  -> watchdog reboot: marker = PASSED, then keep
//                             the IWDG reloaded so the board stays stable
//                             while the host reads RCC_CSR + the marker.
// Host (bench/bringup/watchdog_fire_check.py) requires BOTH the PASSED
// marker and RCC_CSR.IWDGRSTF (bit 29).
#include <Arduino.h>
#include <cstdint>

#include "IWatchdog.h"
#include "stm32f4xx_hal.h"

namespace
{

constexpr std::uint32_t kMagic = 0x5A5A5A5Au;
constexpr std::uint32_t kPhaseArmed = 0xA11EDu; // watchdog armed, expecting fire
constexpr std::uint32_t kPhasePassed = 0x5EEDu; // fire confirmed, keep alive

struct WdgPhase
{
    std::uint32_t magic;
    std::uint32_t phase;
};

// Pinned at 0x2000F000 by the bringup-watchdog env linker flag
// (--section-start=.bram_wdg). Runtime-written RAM, survives NRST and IWDG
// reset; flash_diag.py programs firmware-flash.bin (markers stripped from
// the flash image) so this section is untouched.
__attribute__((section(".bram_wdg"))) volatile WdgPhase g_phase;

constexpr std::uint32_t kReloadMs = 100; // keep-alive once PASSED (2 s window)

} // namespace

void setup()
{
    if (g_phase.magic != kMagic || g_phase.phase != kPhaseArmed)
    {
        // Fresh boot: clear all reset flags so a stale IWDGRSTF cannot
        // false-pass, then arm without reload. IWDG fires in ~2 s (LSI
        // tolerance 17-47 kHz -> up to ~3.8 s).
        RCC->CSR |= RCC_CSR_RMVF; // RM0090 section 6.3.18: write 1 to clear
        g_phase.magic = kMagic;
        g_phase.phase = kPhaseArmed;
        IWatchdog.begin(2'000'000); // 2 s window, microseconds (IWatchdog API)
        return; // no reload: watchdog fires, board resets itself
    }
    // Watchdog reboot (RAM preserved, marker == ARMED): record PASS. The
    // host will read RCC_CSR.IWDGRSTF = 1 plus this marker.
    g_phase.phase = kPhasePassed;
}

void loop()
{
    if (g_phase.phase == kPhasePassed)
    {
        IWatchdog.reload(); // keep alive after PASS recorded - no re-fire loop
    }
    delay(kReloadMs);
}
