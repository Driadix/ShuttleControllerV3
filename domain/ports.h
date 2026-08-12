// Bounded asynchronous ports between domain and adapters (issue #43 section 4,
// budgets #48 section 7). Adapters implement these; the domain core depends on
// the interfaces only (ports-and-adapters, dependencies point inward).
#pragma once

#include <cstdint>

#include "domain/static_queue.h"

namespace slice
{

// Monotonic clock, 1 ms tick, wrap-safe. Single writer: the execution core.
struct Monotonic
{
    // Returns the current monotonic time in ms since power-up/reset.
    virtual std::uint64_t now_ms() const = 0;
};

// Watchdog contract: reload is owned by the execution core at every bounded
// step boundary and in the idle loop (issue #43 section 4, #48 section 3).
struct Watchdog
{
    virtual void reload() = 0;
};

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

// I2C adapter: one slot schedule owner (Sensing Service), bus-busy events from
// the BMS adapter; recovery <= 16 SCL pulses + cooldown >= 5 s (obligation #14).
struct I2cPort
{
    enum class Result : std::uint8_t
    {
        Ok = 0,
        Busy = 1, // BMS TX window or radio audit
        Stuck = 2,
        Recovered = 3,
    };

    // One bounded transaction; never blocks longer than the slot.
    virtual Result read(std::uint8_t device, std::uint8_t reg, std::uint8_t* out, std::uint8_t len) = 0;
    virtual Result recover() = 0; // reinit + <= 16 SCL pulses + cooldown
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
