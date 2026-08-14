// Backup SRAM crash-marker adapter (design docs/safety-authority-design-v3.md
// section 2.3; #45 Q5 A, ticket #71). Persists one 32-bit marker word (payload
// + CRC16, see domain/marker_format.h) in an RTC backup register, preserved
// across system reset (power-cycle != acknowledgment, Q5 A). read_crash is
// read-only for the caller; the init-stamp (permissive CRC-fail -> valid empty)
// happens inside read_crash. Ownership deviation (backup SRAM normally belongs
// to Persistence adapter #43) is owner-approved for the crash marker (design §0 D3).
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class BackupMarker : public SafetyStateMarker
{
  public:
    void write_crash(std::uint32_t crash_count) override;
    SafetyStateMarker::State read_crash() override; // read-only + init-штамп на CRC-fail
    void clear_crash() override;                    // только явный reset-error ack (Фаза 2+)

  private:
    void enable_backup_access();
};

} // namespace v3
