#include "proving/loads.h"

#include <cstdint>

#include "domain/ports.h"
#include "domain/queues.h"
#include "platform/execution_core.h"
#include "proving/fakes.h"

namespace slice
{
namespace proving
{
namespace loads
{

namespace
{

void busy_work(std::uint32_t iterations)
{
    // Deterministic bounded work on host; on target this is replaced by real
    // domain step bodies. Volatile sink prevents the optimizer from eliding it.
    volatile std::uint32_t sink = 0;
    for (std::uint32_t i = 0; i < iterations; ++i)
    {
        sink += i;
    }
    (void)sink;
}

} // namespace

std::uint32_t schedule_operation_steps(std::uint32_t count)
{
    std::uint32_t scheduled = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!kernel::schedule(busy_step, nullptr, 1))
        {
            break;
        }
        ++scheduled;
    }
    return scheduled;
}

void log_storm(QueueClasses& queues, std::uint32_t count)
{
    for (std::uint32_t i = 0; i < count; ++i)
    {
        queues.logs_push(static_cast<Byte>(i));
    }
}

void can_flood(CanPort& can, std::uint32_t count)
{
    CanPort::Frame f = {};
    f.id = 0x100;
    f.len = 8;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        can.tx(f); // TX-side flood; RX flood is injected via inject_rx
    }
}

void tx_backpressure(CanPort& can, std::uint32_t count)
{
    CanPort::Frame f = {};
    f.id = 0x100;
    f.len = 8;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        can.tx(f);
    }
}

void busy_step(void* ctx)
{
    const std::uint32_t iterations = ctx == nullptr ? 1000 : *static_cast<const std::uint32_t*>(ctx);
    busy_work(iterations);
}

} // namespace loads
} // namespace proving
} // namespace slice
