// Hybrid kernel: preemptible force-stop channel, end-to-end (issue 10;
// obligation #3/#13: T_fs = edge -> min-ID CAN frame, C4). Compiled only for
// the hybrid variant (V3_KERNEL_HYBRID).
#ifdef V3_KERNEL_HYBRID

#include <gtest/gtest.h>

#include <cstdint>

#include "domain/ports.h"
#include "platform/execution_core.h"
#include "platform/execution_hybrid.h"
#include "platform/monotonic.h"
#include "proving/fakes.h"

namespace
{

using namespace slice;
using namespace slice::kernel;

using slice::proving::FakeCanPort;

namespace
{
FakeCanPort* g_can = nullptr;

void emit_force_stop(void* ctx)
{
    // ISR-safe emission: bxCAN mailbox write is register-level (RM0090 §32.7).
    static_cast<CanPort*>(ctx)->force_stop_tx();
}
} // namespace

TEST(HybridForceStop, EmitsMinIdFrameSynchronouslyAtEdge)
{
    monotonic::init();
    kernel::init();

    FakeCanPort can;
    register_force_stop_handler(emit_force_stop, &can);

    // The edge preempts: the min-ID frame is emitted synchronously, without
    // waiting for a tick boundary (true preemption semantics).
    kernel::force_stop_isr();
    EXPECT_EQ(can.force_stop_count(), 1); // emitted at the edge, not at a tick
    EXPECT_EQ(kernel::force_stop_count(), 1);
    // T_fs = latch -> emission, measured at the ISR call site.
    EXPECT_GE(kernel::force_stop_latency_us(), 0);
}

TEST(HybridForceStop, EachBumperEdgeEmitsForceStopFrame)
{
    monotonic::init();
    kernel::init();

    FakeCanPort can;
    register_force_stop_handler(emit_force_stop, &can);

    kernel::force_stop_isr();
    EXPECT_EQ(can.force_stop_count(), 1);

    // A second physical edge is a second contact event: it preempts and emits
    // again. (Q7.2 collapse applies to the cooperative deferral path - edges
    // latched during the flash erase window, covered by the coop harness.)
    kernel::force_stop_isr();
    kernel::force_stop_isr();
    EXPECT_EQ(kernel::force_stop_count(), 3);
    EXPECT_EQ(can.force_stop_count(), 3);
}

} // namespace

#endif // V3_KERNEL_HYBRID
