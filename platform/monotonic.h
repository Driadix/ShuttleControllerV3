// Monotonic time policy (design docs/execution-foundation-design-v3.md
// section 2.2, 4.1). Pure policy: delegates to the TimeSource adapter; has no
// Arduino/TIM2/DWT code (include-lint: platform policy leg must not include
// Arduino headers, issue #51 section 5.2). The adapter owns the 64-bit
// aggregation, the CYCCNT seqlock and the ISR (adapters/tim2_clock).
//
// Host: tests inject a fake TimeSource (deterministic); no test hooks in the
// production API (hybrid decision, owner 2026-08-13).
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{
namespace monotonic
{

// Connects the tick source adapter. Must be called once before now_ms/ticks_us.
void init(TimeSource& src);

// Wrap-safe, monotonically non-decreasing ms since init (INV-MONOTONIC).
std::uint64_t now_ms();

// High-resolution source (DWT CYCCNT on target), wrap-safe, us since init.
std::uint64_t ticks_us();

} // namespace monotonic
} // namespace v3
