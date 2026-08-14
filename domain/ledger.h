// Bounded in-memory idempotency ledger (design
// docs/operation-runtime-design-v3.md section 2.4; #13, #47 section 5.1/18 #10;
// ticket #74). Key = (controllerEpoch, resolved authorityId, requestId).
// Per-principal FIFO rings; exhaustion evicts the oldest entry (owner decision
// #74 section 0.1) - a replay after eviction is documentedly not recognized
// (#13: "Если запись вытеснена из bounded ledger, protocol больше не обещает
// распознать повтор"). No persistence across reboot (#13).
#pragma once

#include <cstdint>

namespace v3
{
namespace semantic
{

struct LedgerEntry
{
    std::uint32_t request_id = 0;
    std::uint32_t fingerprint = 0; // codec::request_fingerprint (role, type, parent, params)
    std::uint32_t outcome = 0;     // Accepted: operationId | Rejected: RejectCode
    std::uint8_t kind = 0;         // 0 = accepted, 1 = rejected
};

class IdempotencyLedger
{
  public:
    static constexpr std::uint32_t DepthPerPrincipal = 8; // owner decision #74 §0.1
    static constexpr std::uint32_t MaxPrincipals = 16;    // authorityId budget (#48 §6)
    static constexpr std::uint32_t MaxEntries = MaxPrincipals * DepthPerPrincipal;

    enum class Lookup : std::uint8_t
    {
        Miss = 0,
        SameResult = 1, // replay: return the stored result, no re-admission
        Conflict = 2,   // same key, different fingerprint (#13)
    };

    // SameResult -> out carries the stored result (replay without re-reservation,
    // #13 «возвращает тот же admission result»). Conflict -> no instance created.
    Lookup lookup(std::uint32_t epoch, std::uint16_t authority_id, std::uint32_t request_id,
                  std::uint32_t fp, LedgerEntry& out) const;
    // Store only after the admission outcome is fixed; REJECTED results are
    // stored too, so a replay after gates changed returns the same rejection.
    void store(std::uint32_t epoch, std::uint16_t authority_id, const LedgerEntry& e);

    std::uint32_t used(std::uint16_t authority_id) const;
    std::uint32_t evicted_count() const { return m_evicted; }

  private:
    struct Slot
    {
        std::uint32_t epoch = 0;
        LedgerEntry entry;
    };
    Slot m_rings[MaxPrincipals][DepthPerPrincipal];
    std::uint8_t m_head[MaxPrincipals] = {};
    std::uint8_t m_count[MaxPrincipals] = {};
    std::uint32_t m_evicted = 0;
};

} // namespace semantic
} // namespace v3
