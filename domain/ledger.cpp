// Idempotency ledger implementation (design
// docs/operation-runtime-design-v3.md section 2.4, 3.6; ticket #74).
#include "domain/ledger.h"

namespace v3
{
namespace semantic
{

IdempotencyLedger::Lookup IdempotencyLedger::lookup(std::uint32_t epoch,
                                                    std::uint16_t authority_id,
                                                    std::uint32_t request_id,
                                                    std::uint32_t fp,
                                                    LedgerEntry& out) const
{
    if (authority_id >= MaxPrincipals)
    {
        return Lookup::Miss;
    }
    const std::uint8_t count = m_count[authority_id];
    for (std::uint8_t i = 0; i < count; ++i)
    {
        const std::uint8_t idx = static_cast<std::uint8_t>((m_head[authority_id] + i) % DepthPerPrincipal);
        const Slot& s = m_rings[authority_id][idx];
        if (s.epoch == epoch && s.entry.request_id == request_id)
        {
            if (s.entry.fingerprint == fp)
            {
                out = s.entry;
                return Lookup::SameResult;
            }
            return Lookup::Conflict;
        }
    }
    return Lookup::Miss;
}

void IdempotencyLedger::store(std::uint32_t epoch, std::uint16_t authority_id, const LedgerEntry& e)
{
    if (authority_id >= MaxPrincipals)
    {
        return; // impossible in production: authorityId is controller-assigned (<= 16)
    }
    Slot& tail = m_rings[authority_id][static_cast<std::uint8_t>((m_head[authority_id] + m_count[authority_id]) % DepthPerPrincipal)];
    if (m_count[authority_id] >= DepthPerPrincipal)
    {
        // FIFO eviction: overwrite head, advance head (oldest dropped first).
        m_head[authority_id] = static_cast<std::uint8_t>((m_head[authority_id] + 1) % DepthPerPrincipal);
        ++m_evicted;
        Slot& evicted = m_rings[authority_id][static_cast<std::uint8_t>((m_head[authority_id] + m_count[authority_id] - 1) % DepthPerPrincipal)];
        evicted.epoch = epoch;
        evicted.entry = e;
        return;
    }
    tail.epoch = epoch;
    tail.entry = e;
    ++m_count[authority_id];
}

std::uint32_t IdempotencyLedger::used(std::uint16_t authority_id) const
{
    if (authority_id >= MaxPrincipals)
    {
        return 0;
    }
    return m_count[authority_id];
}

} // namespace semantic
} // namespace v3
