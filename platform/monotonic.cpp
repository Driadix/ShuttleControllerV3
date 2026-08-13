// Monotonic time policy implementation: pure delegation to the TimeSource
// adapter (design section 2.2, 4.1). All timing state (64-bit tick, DWT
// CYCCNT seqlock, TIM2 ISR) lives in adapters/tim2_clock; this TU contains no
// Arduino code and no ISR. Single implementation for host and target: the
// adapter interface is the only seam (host fakes implement TimeSource).
#include "platform/monotonic.h"

namespace v3
{
namespace monotonic
{
namespace
{

TimeSource* g_src = nullptr;

} // namespace

void init(TimeSource& src)
{
    g_src = &src;
    g_src->init_tick();
}

std::uint64_t now_ms() { return g_src->raw_now_ms(); }

std::uint64_t ticks_us() { return g_src->raw_ticks_us(); }

} // namespace monotonic
} // namespace v3
