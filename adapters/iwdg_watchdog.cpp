// IWDG watchdog adapter implementation (target-only, Arduino Core).
#include "adapters/iwdg_watchdog.h"

#ifdef ARDUINO

#include "IWatchdog.h"

namespace v3
{

void IwdgWatchdog::init(std::uint32_t window_us)
{
    // IWDG 10 s nominal; hardware range 6.8-18.8 s across LSI tolerance
    // (issue #48 section 3). The policy budgets on the fast 6.8 s end.
    IWatchdog.begin(window_us);
}

void IwdgWatchdog::reload()
{
    IWatchdog.reload();
}

} // namespace v3

#endif // ARDUINO
