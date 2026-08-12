// Target entry point for the proving slice. Target-only: the native host envs
// exclude this file via build_src_filter (-<platform/main.cpp>), so no ARDUINO
// guard is needed here.
// Bring-up order (issue #43 section 5): monotonic tick -> kernel -> harness.
//
// The measurement scenario (proving/scenario.cpp) is driven here once the real
// adapters land (bench bring-up, issue #54): CAN adapter for the C1 chain
// emission + diagnostic serial for measurement records. Until then the target
// leg is an empty kernel loop and the obligations #1-#6/#8/#10/#12-#14 are
// explicitly transferred to the bench step (proving-slice-v3.md section 6).
#include <Arduino.h>

#include "platform/execution_core.h"
#include "platform/monotonic.h"

void setup()
{
    slice::monotonic::init();
    slice::kernel::init();
}

void loop()
{
    slice::kernel::run(); // never returns (cooperative idle loop)
}
