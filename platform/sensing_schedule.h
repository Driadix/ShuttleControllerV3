// Composition-root glue for the sensing slice (design
// docs/sensing-slice-design-v3.md sections 5.3, 6; ticket #63). Host-buildable
// (no Arduino): schedules the SensingService bounded step via kernel::schedule
// and re-arms after every execution. The domain stays framework-free
// (dependencies inward, issue #43).
//
// The re-arm deadline is computed from a FRESH monotonic read AFTER the step:
// a long step (e.g. a stuck bus blocking the I2C transaction) must not make
// the deadline stale - the kernel rejects out-of-window deadlines silently.
// A rejected schedule is re-armed with an in-window now+1 deadline so the
// acquisition can never die without a trace (review MAJOR fix, #63).
//
// L1 observability (HITL decision #63-§0.3, RAM read-back): the glue also
// mirrors the service snapshots into a pinned diagnostic struct
// (.bram_sensing, 52 words, layout compatible with the sensing-proto
// contract) that the verification runner reads over OpenOCD - the UART is
// silent in Phase 1. The mirror is written by diag_words() (pure, host
// testable) into a target section here.
#pragma once

#include <cstdint>

#include "domain/sensing.h"

namespace v3
{
namespace sensing
{

// Kernel step callback (kernel::schedule target). Calls step(now) on the
// service, then re-schedules with a fresh deadline; on rejection, re-arms
// with now+1. Never returns (re-arms itself).
void schedule_tick(void* ctx);

// Diagnostic mirror (L1 readback): writes magic/version/uptime, summed
// i2c_reads/i2c_fails and the 5 sensor snapshots (8 words each: raw, raw2,
// age_ms, state, samples_ok, samples_fail, last_status, sample_ms) into
// `out` (52 words). Pure, deterministic - host testable. Returns the word
// count written.
std::uint32_t diag_words(const SensingService& svc, std::uint64_t now,
                         std::uint32_t* out, std::uint32_t capacity);

} // namespace sensing
} // namespace v3
