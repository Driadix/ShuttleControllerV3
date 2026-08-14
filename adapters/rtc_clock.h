// Wall-clock adapter (design docs/observability-design-v3.md sections 2.1, 4.2;
// ticket #72). Read-only RTC epoch + time validity. SetWallClock (mutation) is
// a Service-class op, outside #72 (#75/#76). TimeValidity: Unsynced until a
// SetWallClock lands in the current epoch AND the RTC calendar is initialized
// (INITS); a running-but-never-synced RTC gives RtcOnly; a dead/uninitialized
// RTC (no LSE, no VBAT, V1 2023-01-01 defect) gives Unsynced with wall=0 - the
// architecture forbids emitting a convincing time from a dead battery (#49 s3).
//
// Register-level read (no HAL RTC init): calendar initialization belongs to
// #76 (lifecycle / RTC power); this adapter only READS an initialized calendar.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class RtcClock : public WallClockSource
{
  public:
    void init(); // no-op: read-only; validity derived from RTC state each read
    std::uint32_t epoch_sec() const override;
    codec::TimeValidity time_validity() const override;
    // Called when the Service layer performs SetWallClock (#75/#76): flips the
    // per-epoch synced flag. Not part of #72 production paths.
    void mark_synced() { m_synced_this_epoch = true; }

  private:
    bool m_synced_this_epoch = false; // SetWallClock seen this epoch
};

} // namespace v3
