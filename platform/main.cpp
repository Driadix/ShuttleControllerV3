// Target entry point for the proving slice. Target-only: the native host envs
// exclude this file via build_src_filter (-<platform/main.cpp>), so no ARDUINO
// guard is needed here.
// Bring-up order (issue #43 section 5): monotonic tick -> kernel -> harness.
//
// The measurement scenario (proving/scenario.cpp) is driven here once the real
// adapters land (bench bring-up, issue #54): CAN adapter for the C1 chain
// emission + diagnostic serial for measurement records. Until then the target
// leg runs the kernel (coop/hybrid) or the RTOS task set (rtos) with no
// adapters, and the obligations #1-#6/#8/#10/#12-#14 are explicitly
// transferred to the bench step (proving-slice-v3.md section 6).
#include <Arduino.h>

#include "platform/execution_core.h"
#include "platform/monotonic.h"

#ifdef V3_KERNEL_RTOS
#include "proving/harness.h"
#include "proving/rtos_harness.h"
#endif

void setup()
{
    slice::monotonic::init();
    slice::kernel::init();

#ifdef V3_KERNEL_RTOS
    // Static storage: outlives the scheduler (never returns).
    static slice::proving::HarnessState rtos_state;
    slice::proving::rtos_harness::start(rtos_state); // never returns
#endif
}

void loop()
{
    slice::kernel::run(); // never returns (cooperative/hybrid idle loop)
}
