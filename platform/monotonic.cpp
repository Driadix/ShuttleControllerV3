#include "platform/monotonic.h"

#include <cstdint>

#ifdef ARDUINO

// Target leg: TIM2 (32-bit) at 1 ms via the core's HardwareTimer (the core owns
// TIM2_IRQHandler through SrcWrapper/HAL - a custom handler would collide).
// Does not disturb SysTick (core millis). DWT CYCCNT provides cycle-accurate
// ticks_us (Cortex-M4F, 168 MHz on genericSTM32F405RG).
#include <Arduino.h>
#include <HardwareTimer.h>

namespace slice
{
namespace monotonic
{
namespace
{

volatile std::uint64_t g_now_ms = 0;
HardwareTimer g_tim2(TIM2);

void on_tick()
{
    ++g_now_ms;
}

} // namespace

void init()
{
    // Enable the DWT cycle counter.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    g_tim2.setOverflow(1000, MICROSEC_FORMAT); // 1 ms
    g_tim2.attachInterrupt(on_tick);
    g_tim2.resume();
}

std::uint64_t now_ms() { return g_now_ms; }

std::uint64_t ticks_us() { return DWT->CYCCNT / 168; } // 168 MHz => us

void test_set_time_ms(std::uint64_t) {}
void test_advance_ms(std::uint64_t) {}

} // namespace monotonic
} // namespace slice

#else

// Host leg: virtual injected clock for deterministic tests; real steady clock
// only for ticks_us (host benchmark mode, explicitly non-deterministic).
#include <chrono>

namespace slice
{
namespace monotonic
{
namespace
{

std::uint64_t g_now_ms = 0;

std::uint64_t steady_us()
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

void init() { g_now_ms = 0; }

std::uint64_t now_ms() { return g_now_ms; }

std::uint64_t ticks_us() { return steady_us(); }

void test_set_time_ms(std::uint64_t t) { g_now_ms = t; }
void test_advance_ms(std::uint64_t dt) { g_now_ms += dt; }

} // namespace monotonic
} // namespace slice

#endif
