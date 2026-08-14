// Exclusive Control Activity slot (design docs/operation-runtime-design-v3.md
// section 2.6; #46 I-LC-4: at most one exclusive activity at any moment).
// Framework-free. Claimed by the Operation Runtime for exclusive roots;
// Manual Session (#77) and Update (#76) use the same module. Queries,
// subscriptions and read-only diagnostics do NOT take the slot (#46 section 8).
// Single-threaded foreground: no locking needed (#43 section 4).
#pragma once

#include <cstdint>

namespace v3
{
namespace slot
{

enum class Activity : std::uint8_t
{
    Idle = 0,
    Motion = 1,
    ManualSession = 2,
    Service = 3,
    Update = 4,
};

class ExclusiveSlot
{
  public:
    // Idle -> a; busy -> false (caller answers ResourceConflict).
    bool try_claim(Activity a);
    // Only the owner releases; mismatch is a no-op (single-writer invariant).
    void release(Activity a);
    Activity current() const { return m_current; }

  private:
    Activity m_current = Activity::Idle;
};

} // namespace slot
} // namespace v3
