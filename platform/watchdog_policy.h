// Watchdog reload policy (design docs/execution-foundation-design-v3.md
// section 2.3, 4.1). Reload is owned by the execution core: called at every
// bounded step boundary and in the idle loop, always from the foreground
// (INV-WATCHDOG-ARMED, issue #43 section 4, #48 section 3). The hardware IWDG
// lives in adapters/iwdg_watchdog; this policy has no Arduino code and no ISR.
//
// Window model: IWDG 10 s nominal, hardware range 6.8-18.8 s across LSI
// tolerance; budgets reload on the fast 6.8 s end (issue #48 section 3).
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{
namespace watchdog
{

// Arms the IWDG (10 s, adapter) and starts the reload model.
void init(WatchdogPort& hw);

// Reloads the IWDG. Called only by the execution core, only from foreground.
void reload();

// Marks the start of a flash window (W_flash ~ 4 s, blocking). Ensures a
// reload between consecutive flash windows: two windows back-to-back
// (~ 8 s) exceed the 6.8 s fast end, so a reload is issued before the window
// when needed (issue #48 section 3).
void note_flash_window();

// Step overrun (step > T_step): on target the hardware IWDG is the backstop
// (no-op here); the starvation model lives in the host fake (test_watchdog).
void report_overrun(std::uint32_t step_ms);

} // namespace watchdog
} // namespace v3
