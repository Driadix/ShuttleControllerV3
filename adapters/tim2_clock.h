// TIM2 clock adapter: implements v3::TimeSource for the target (design
// docs/execution-foundation-design-v3.md sections 1, 3.1, 4.2). The ONLY ISR
// in scope #70 (rule R2): it advances the 64-bit tick and publishes the DWT
// CYCCNT seqlock snapshot, and nothing else - no kernel call, no watchdog
// reload, no event emission (T15). All policy runs in the foreground.
//
// Arduino API is confined to this adapter (#51 section 5); platform policy
// (monotonic.h/.cpp) has no Arduino/TIM2 code.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class Tim2Clock : public TimeSource
{
  public:
    void init_tick() override;             // TIM2 1 ms + DWT CYCCNT enable
    std::uint64_t raw_now_ms() override;   // 64-bit tick, wrap-safe
    std::uint64_t raw_ticks_us() override; // CYCCNT-derived, wrap-safe
};

} // namespace v3
