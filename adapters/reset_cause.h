// Reset-cause adapter: implements v3::ResetCauseSource for the target (design
// docs/execution-foundation-design-v3.md section 4.2). Reads the RCC reset
// flags once at startup (foreground); the execution core reports the cause
// through KernelEvents::reset_cause (crash record, #49 section 13).
#pragma once

#include "domain/ports.h"

namespace v3
{

class Stm32ResetCause : public ResetCauseSource
{
  public:
    ResetCause read() override;
};

} // namespace v3
