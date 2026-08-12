// Target entry point for the proving slice. Target-only: the native host envs
// exclude this file via build_src_filter (-<platform/main.cpp>), so no ARDUINO
// guard is needed here.
// Bring-up order (issue #43 section 5): monotonic tick -> kernel -> harness.
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
