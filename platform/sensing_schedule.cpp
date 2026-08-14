// Composition-root glue implementation (see sensing_schedule.h). The kernel
// runs at most one bounded step per tick; this glue keeps the sensing slot
// alive across long steps and schedule rejections (review MAJOR fix, #63),
// and mirrors the service snapshots into the L1 readback struct (HITL §0.3).
#include "platform/sensing_schedule.h"

#include "platform/execution_core.h"
#include "platform/monotonic.h"

namespace v3
{
namespace sensing
{
namespace
{

// Production diagnostic struct (design section 5.1, HITL decision §0.3):
// snapshots + counters for the L1 readback scenario (runner v2). Layout
// matches the sensing-proto contract for the offsets the scenario checks
// (magic@0, state sensor N @ base+3, samples_ok @ base+4, step duration
// words 8/9/10). Pinned at 0x20011000 by the firmware env linker flag
// (--section-start .bram_sensing); stripped from the flash image by the
// runner (tools/flash.py) - runtime RAM only, never flashed.
__attribute__((section(".bram_sensing"))) volatile std::uint32_t g_sensing_diag[52];

constexpr std::uint32_t kDiagMagic = 0x53454E53u; // 'SENS'
constexpr std::uint32_t kDiagVersion = 2u;
constexpr std::uint32_t kDiagHeaderWords = 12u;
constexpr std::uint32_t kDiagSensorWords = 8u;
constexpr std::uint32_t kDiagWordCount = 52u;

// Observed step duration (bounded-step evidence, L1 max rule vs T_step):
// accumulated in the glue across every step, mirrored into words 8/9/10.
std::uint32_t g_step_ms_max = 0u;
std::uint32_t g_step_ms_total = 0u;
std::uint32_t g_step_count = 0u;

} // namespace

void schedule_tick(void* ctx)
{
    auto* svc = static_cast<SensingService*>(ctx);

    const std::uint64_t now = monotonic::now_ms();
    svc->step(now);

    // L1 mirror (fresh snapshots; the runner reads this over OpenOCD).
    const std::uint32_t words = diag_words(*svc, now,
                                           const_cast<std::uint32_t*>(g_sensing_diag),
                                           kDiagWordCount);

    // Fresh deadline from a post-step clock read: a step that overran the
    // slot (stuck bus blocking the I2C transaction) must still re-arm with
    // a deadline inside the kernel window [now, now + T_step].
    const std::uint64_t now2 = monotonic::now_ms();
    const std::uint32_t step_ms = static_cast<std::uint32_t>(now2 - now);
    if (step_ms > g_step_ms_max)
    {
        g_step_ms_max = step_ms;
    }
    g_step_ms_total += step_ms;
    ++g_step_count;
    if (words == kDiagWordCount)
    {
        // Header words 8/9/10: observed step duration (bounded-step
        // evidence, L1 max rule vs T_step 10 ms).
        g_sensing_diag[8] = g_step_ms_max;
        g_sensing_diag[9] = g_step_ms_total;
        g_sensing_diag[10] = g_step_count;
    }
    const std::uint32_t deadline =
        static_cast<std::uint32_t>(now2) + svc->next_step_ms();
    if (kernel::schedule(&schedule_tick, ctx, deadline) != kernel::ScheduleResult::Ok)
    {
        // Queue full (kernel emitted schedule_rejected) or deadline out of
        // window: re-arm with a fresh in-window deadline. The acquisition
        // must not die silently - recovery depends on future slots.
        (void)kernel::schedule(&schedule_tick, ctx,
                               static_cast<std::uint32_t>(now2) + 1u);
    }
}

std::uint32_t diag_words(const SensingService& svc, std::uint64_t now,
                         std::uint32_t* out, std::uint32_t capacity)
{
    if (out == nullptr || capacity < kDiagWordCount)
    {
        return 0u;
    }
    std::uint32_t reads = 0u;
    std::uint32_t fails = 0u;
    for (std::uint32_t i = 0; i < kDiagWordCount; ++i)
    {
        out[i] = 0u;
    }
    for (std::uint32_t i = 0; i < 5u; ++i)
    {
        SensorSnapshot s;
        (void)svc.get_snapshot(static_cast<SensorId>(i), &s);
        reads += s.samples_ok;
        fails += s.samples_fail;
        const std::uint32_t base = kDiagHeaderWords + i * kDiagSensorWords;
        out[base + 0u] = s.raw;
        out[base + 1u] = s.raw2;
        out[base + 2u] = s.age_ms;
        out[base + 3u] = static_cast<std::uint32_t>(s.state);
        out[base + 4u] = s.samples_ok;
        out[base + 5u] = s.samples_fail;
        out[base + 6u] = s.last_status;
        out[base + 7u] = static_cast<std::uint32_t>(s.sample_ms);
    }
    out[0] = kDiagMagic;
    out[1] = kDiagVersion;
    out[2] = static_cast<std::uint32_t>(now);
    out[3] = svc.recovery_count(); // recovery attempts (L1 eq 0 in a healthy window)
    out[4] = reads;
    out[5] = fails;
    out[8] = 0u; // step_ms_max (filled by schedule_tick)
    out[9] = 0u; // step_ms_total
    out[10] = 0u; // step_count
    return kDiagWordCount;
}

} // namespace sensing
} // namespace v3
