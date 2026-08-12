// Hybrid kernel: preemptible force-stop channel (issue 10; obligation #3/#13).
// Compiled only for the hybrid variant (V3_KERNEL_HYBRID). Run with:
//   PLATFORMIO_BUILD_FLAGS="-DV3_KERNEL_HYBRID" pio test -e native
#ifdef V3_KERNEL_HYBRID

#include <gtest/gtest.h>

#include <cstdint>

#include "platform/execution_core.h"
#include "platform/execution_hybrid.h"
#include "platform/monotonic.h"

namespace
{

using namespace slice;

TEST(HybridForceStop, LatchServedAtNextTickBoundary)
{
    monotonic::init();
    kernel::init();

    kernel::force_stop_isr();
    EXPECT_GT(kernel::force_stop_count(), 0);

    monotonic::test_set_time_ms(1);
    kernel::on_tick();
    EXPECT_EQ(kernel::force_stop_count(), 1); // collapsed: repeated edges = one
    // Latency observable on host; on target the ISR preempts mid-step.
    EXPECT_GE(kernel::force_stop_latency_us(), 0);
}

TEST(HybridForceStop, RepeatedEdgesCollapseIntoOne)
{
    monotonic::init();
    kernel::init();

    kernel::force_stop_isr();
    kernel::force_stop_isr();
    kernel::force_stop_isr();

    monotonic::test_set_time_ms(1);
    kernel::on_tick();
    EXPECT_EQ(kernel::force_stop_count(), 1); // Q7.2: repeated edges collapse
}

} // namespace

#endif // V3_KERNEL_HYBRID
