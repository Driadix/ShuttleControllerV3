// Bounded asynchronous ports between domain and adapters (issue #43 section 4,
// budgets #48 section 7). Adapters implement these; the domain core depends on
// the interfaces only (ports-and-adapters, dependencies point inward).
#pragma once

#include <cstdint>

#include "domain/static_queue.h"

namespace slice
{

// CAN adapter (issue #43 section 4): bounded TX (<= 16 frames/tick), bounded
// RX drain (<= 64 frames/tick), force-stop on a dedicated mailbox with the
// minimum extended ID, outside all queues.
struct CanPort
{
    struct Frame
    {
        std::uint32_t id = 0;   // extended ID
        std::uint8_t data[8] = {};
        std::uint8_t len = 0;
    };

    // Bounded TX: returns false when the per-tick budget is exhausted.
    virtual bool tx(const Frame& frame) = 0;

    // Bounded RX drain: pops at most `budget` frames; returns number drained.
    virtual std::uint32_t rx_drain(Frame* out, std::uint32_t budget) = 0;

    // Fault-injection hook (harness L1): pushes a frame into the RX path as if
    // received from the bus. Target adapter implements it as a test hook
    // behind a compile-time flag; host fakes implement it directly.
    virtual void inject_rx(const Frame& frame) = 0;

    // Force-stop: dedicated mailbox, lowest extended ID on the bus.
    virtual void force_stop_tx() = 0;
};

// UART transport adapter: byte budget per tick (display 230, radio 57, BMS 10).
// Never blocks: DMA or producer-budget (issue #43 section 4, obligation #12).
struct UartPort
{
    virtual std::uint32_t tx_bytes_available() const = 0;
    virtual bool tx(const std::uint8_t* data, std::uint32_t len) = 0;
    virtual std::uint32_t rx_drain(std::uint8_t* out, std::uint32_t budget) = 0;
};

// Flash persistence adapter (journal, sector 7, 128 KB, 512 B pages).
// Quiescence C6: save window <= 4 s; single atomic blocking window.
struct FlashPort
{
    virtual std::uint32_t erase_sector() = 0;  // returns measured ms
    virtual std::uint32_t program_page(const std::uint8_t* data) = 0; // returns measured ms
};

// Observability sink: bounded queues with per-class overload policy
// (telemetry 8 drop-oldest, events 32 / logs 32 drop-newest, traces 16).
// Counters live here; every drop/reject increments a counter (issue #43 section 6).
struct ObservabilityPort
{
    enum class Class : std::uint8_t
    {
        Telemetry = 0,
        Events = 1,
        Logs = 2,
        Traces = 3,
    };

    virtual bool emit(Class cls, const std::uint8_t* data, std::uint32_t len) = 0;
    virtual std::uint32_t dropped(Class cls) const = 0;
};

} // namespace slice

// ---------------------------------------------------------------------------
// Production ports (design docs/execution-foundation-design-v3.md section 4.2).
// Implemented by adapters (adapters/); the platform policy and the domain core
// depend on these interfaces only. All calls are foreground-only, never from
// an ISR (rule R2, issue #43 section 3.2).
// ---------------------------------------------------------------------------
namespace v3
{

// Monotonic tick source: implemented by adapters/tim2_clock. The adapter owns
// the 64-bit aggregation and the DWT CYCCNT seqlock; policy delegates
// (platform/monotonic.h has no Arduino/TIM2 code).
struct TimeSource
{
    virtual void init_tick() = 0;             // TIM2 1 ms, DWT enable (adapter)
    virtual std::uint64_t raw_now_ms() = 0;   // 64-bit aggregated tick (adapter keeps 64-bit)
    virtual std::uint64_t raw_ticks_us() = 0; // CYCCNT-derived, wrap-safe
};

// Watchdog hardware: implemented by adapters/iwdg_watchdog.
struct WatchdogPort
{
    virtual void init(std::uint32_t window_us) = 0; // IWDG 10 s
    virtual void reload() = 0;
};

// Reset-cause: implemented by adapters/reset_cause. Read once at startup.
enum class ResetCause : std::uint8_t
{
    PowerOn = 0,
    Watchdog = 1,
    Software = 2,
    External = 3,
    Unknown = 4,
};

struct ResetCauseSource
{
    virtual ResetCause read() = 0; // called by the execution core at startup (foreground)
};

// Kernel event sink (outbound port, design section 2.4). Foreground-only calls.
// Phase 1: stub (no-op diagnostic sink); Phase 2: Observability Producer (#72).
struct KernelEvents
{
    virtual void step_overrun(std::uint32_t step_ms) = 0; // step > T_step (obs #8)
    virtual void scheduler_gap(std::uint64_t gap_ms) = 0; // process_tick entry delayed > 3xT_step (30 ms)
    virtual void schedule_rejected() = 0;                 // step queue full (obs #7)
    virtual void reset_cause(ResetCause cause) = 0;       // startup crash record through reboot (#49 section 13)
};

// Mandatory safety boundary (design section 2.5): freshness-check + arbitration
// on every step boundary, OUTSIDE the FIFO (INV-SENSING-FRESH, #48 section 4).
struct SafetySlot
{
    virtual void tick(std::uint64_t now) = 0; // called by the execution core on every step boundary, foreground only
};

// I2C adapter (sensing slice #63, design docs/sensing-slice-design-v3.md):
// one slot-schedule owner - the Sensing Service; Busy is reserved for the BMS
// TX window / radio audit (Phase 2, #48 section 7). Recovery: reinit + <= 16
// SCL pulses + cooldown >= 5 s (obligation #14). Production form of the
// former slice::I2cPort (no callers existed; clean cutover, #85 decision).
// Status classification follows V1 TOF_Sense.cpp: noack (address/data),
// short (under-read), stuck (HAL BUSY/TIMEOUT/ERROR - recover candidate).
enum class I2cResult : std::uint8_t
{
    Ok = 0,        // transaction completed (STOP), data valid
    NoAck = 1,     // device did not acknowledge (absent/unpowered) - no recovery
    Short = 2,     // read phase under-delivered - no recovery
    Busy = 3,      // bus held by another owner (BMS TX / radio audit, Phase 2)
    Stuck = 4,     // bus in an invalid state (HAL BUSY/TIMEOUT/ERROR) - recover()
    Recovered = 5, // after recover(): bus re-initialized
};

struct I2cPort
{
    // One bounded transaction: write reg, restart, read len (STOP at the end).
    // Never blocks longer than the slot (<= T_step).
    virtual I2cResult read(std::uint8_t device, std::uint8_t reg,
                           std::uint8_t* out, std::uint8_t len) = 0;
    // Reinit + <= 16 SCL pulses + STOP + cooldown (issue #48 section 7,
    // obligation #14). Called only by the Sensing Service, foreground only.
    virtual I2cResult recover() = 0;
    // Raw last Wire/HAL status of the last transaction (diagnostics/evidence).
    virtual std::uint8_t last_wire_status() const = 0;
};

} // namespace v3
