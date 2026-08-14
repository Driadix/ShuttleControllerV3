// Idempotency ledger tests (design docs/operation-runtime-design-v3.md
// section 7.3 T14-T18; #13, #47 section 5.1/18 #10): per-principal isolation,
// same-result replay, conflict, FIFO eviction, rejected outcomes stored.
#include <cstdint>

#include <gtest/gtest.h>

#include "domain/codec.h"
#include "domain/ledger.h"

namespace
{

using v3::semantic::IdempotencyLedger;
using v3::semantic::LedgerEntry;

TEST(Ledger, PerPrincipalIsolation)
{
    IdempotencyLedger l;
    LedgerEntry e;
    e.request_id = 42;
    e.fingerprint = 0x1111u;
    e.outcome = 7;
    e.kind = 0;
    l.store(9, 1, e);

    // Same key on a DIFFERENT principal: miss (#47 section 18 #10).
    LedgerEntry out;
    EXPECT_EQ(l.lookup(9, 2, 42, 0x1111u, out), IdempotencyLedger::Lookup::Miss);
    // Same principal, same key: hit.
    EXPECT_EQ(l.lookup(9, 1, 42, 0x1111u, out), IdempotencyLedger::Lookup::SameResult);
    EXPECT_EQ(out.outcome, 7u);
    EXPECT_EQ(out.kind, 0u);
}

TEST(Ledger, EpochIsPartOfTheKey)
{
    IdempotencyLedger l;
    LedgerEntry e;
    e.request_id = 5;
    e.fingerprint = 1;
    e.outcome = 11;
    e.kind = 0;
    l.store(9, 1, e);
    LedgerEntry out;
    // Same requestId in another epoch: miss (epoch fencing, #13).
    EXPECT_EQ(l.lookup(10, 1, 5, 1, out), IdempotencyLedger::Lookup::Miss);
}

TEST(Ledger, SameResultOnSameFingerprint)
{
    IdempotencyLedger l;
    LedgerEntry e;
    e.request_id = 100;
    e.fingerprint = 0xCAFEu;
    e.outcome = 0xABCDEF01u; // operationId
    e.kind = 0;
    l.store(9, 3, e);
    LedgerEntry out;
    EXPECT_EQ(l.lookup(9, 3, 100, 0xCAFEu, out), IdempotencyLedger::Lookup::SameResult);
    EXPECT_EQ(out.outcome, 0xABCDEF01u);
}

TEST(Ledger, ConflictOnDifferentFingerprint)
{
    IdempotencyLedger l;
    LedgerEntry e;
    e.request_id = 7;
    e.fingerprint = 0xAAAAu;
    e.outcome = 1;
    e.kind = 0;
    l.store(9, 1, e);
    LedgerEntry out;
    // Same key, different payload: Conflict (#13), no instance.
    EXPECT_EQ(l.lookup(9, 1, 7, 0xBBBBu, out), IdempotencyLedger::Lookup::Conflict);
}

TEST(Ledger, FifoEvictionAfterDepth)
{
    IdempotencyLedger l;
    LedgerEntry out;
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        LedgerEntry e;
        e.request_id = static_cast<std::uint32_t>(1000 + i);
        e.fingerprint = static_cast<std::uint32_t>(i);
        e.outcome = static_cast<std::uint32_t>(i);
        e.kind = 0;
        l.store(9, 1, e);
        EXPECT_EQ(l.used(1), i + 1);
    }
    EXPECT_EQ(l.evicted_count(), 0u);
    // 9th store evicts the oldest (requestId 1000): FIFO (#13: replay after
    // eviction is documentedly not recognized).
    LedgerEntry ninth;
    ninth.request_id = 9999;
    ninth.fingerprint = 0x9;
    ninth.outcome = 9;
    ninth.kind = 0;
    l.store(9, 1, ninth);
    EXPECT_EQ(l.evicted_count(), 1u);
    EXPECT_EQ(l.used(1), 8u);
    EXPECT_EQ(l.lookup(9, 1, 1000, 0, out), IdempotencyLedger::Lookup::Miss);
    EXPECT_EQ(l.lookup(9, 1, 1001, 1, out), IdempotencyLedger::Lookup::SameResult);
    EXPECT_EQ(l.lookup(9, 1, 9999, 0x9, out), IdempotencyLedger::Lookup::SameResult);
}

TEST(Ledger, RejectedOutcomeStoredForReplay)
{
    IdempotencyLedger l;
    LedgerEntry e;
    e.request_id = 55;
    e.fingerprint = 0x77;
    e.outcome = static_cast<std::uint32_t>(v3::codec::RejectCode::HealthGate);
    e.kind = 1; // rejected
    l.store(9, 4, e);
    LedgerEntry out;
    EXPECT_EQ(l.lookup(9, 4, 55, 0x77, out), IdempotencyLedger::Lookup::SameResult);
    EXPECT_EQ(out.kind, 1u);
    EXPECT_EQ(out.outcome, static_cast<std::uint32_t>(v3::codec::RejectCode::HealthGate));
}

} // namespace
