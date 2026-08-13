// IWDG watchdog adapter: implements v3::WatchdogPort for the target (design
// docs/execution-foundation-design-v3.md section 4.2). Reload policy lives in
// platform/watchdog_policy.cpp (execution core owns reloads); this adapter is
// the hardware boundary only. Arduino API is confined to this adapter.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class IwdgWatchdog : public WatchdogPort
{
  public:
    void init(std::uint32_t window_us) override; // IWDG 10 s (us units)
    void reload() override;
};

} // namespace v3
