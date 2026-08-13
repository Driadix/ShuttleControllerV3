// Shared kernel test environment (design section 7.2): wires the fakes into
// kernel::init and exposes them for assertions.
#pragma once

#include "platform/execution_core.h"
#include "tests/common/fakes.h"

namespace testfakes
{

struct KernelEnv
{
    TestTimeSource time;
    FakeWatchdog hw;
    RecordingEvents events;
    FakeResetCause reset;
    FakeSafety safety;

    KernelEnv() : hw(time), reset(v3::ResetCause::PowerOn) {}

    void init(v3::ResetCause cause = v3::ResetCause::PowerOn)
    {
        reset = FakeResetCause(cause);
        hw = FakeWatchdog(time);
        events.clear();
        const v3::kernel::KernelConfig cfg{&time, &hw, &events, &reset, &safety};
        v3::kernel::init(cfg);
    }
};

} // namespace testfakes
