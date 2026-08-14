// CAN bus adapter: implements v3::CanPort for the target (design
// docs/safety-authority-design-v3.md section 2.4; ticket #71). Arduino/HAL API
// confined to this adapter (issue #51 section 5). Wiring per bench record #73:
// CAN1 RX = PB8, TX = PB9 (AF9), 500 kbit/s extended, netlist contract.
//
// Bounded TX (<= 16 frames/tick, #48 §7), bounded RX drain (<= 64/tick),
// force-stop via min extended ID (0x1, INV-FORCE-STOP-CHANNEL) with ABRQ
// preemption of pending normal TX (never queued behind control frames), and
// CAN error-state classification (Q7.1: error counters, bus-off recovery).
//
// Phase-1 adapter contract note: with no CAN peer on the bench (#62 absent)
// and <= 2 emitted frames/tick, mailbox contention is unreal - the "dedicated
// mailbox" hardware reservation (mailbox 0) is deferred to the CAN-peer era;
// the functional guarantee (force-stop preempts pending TX via ABRQ, min ID
// wins bus arbitration) holds here.
#pragma once

#include <cstdint>

#include "domain/diag_safety.h"
#include "domain/ports.h"

namespace v3
{

class CanBus : public CanPort
{
  public:
    void init(bool loopback, SafetyDiag* diag); // CAN_MODE_LOOPBACK for L4 legs, NORMAL otherwise

    bool tx(const CanFrame& frame) override;
    std::uint32_t rx_drain(CanFrame* out, std::uint32_t budget) override;
    void force_stop_tx() override;
    CanErrorState error_state() const override;
    void recover_bus_off() override;

  private:
    SafetyDiag* m_diag = nullptr;
    std::uint64_t m_budget_reset_ms = 0; // monotonic window anchor (per-tick budget)
    std::uint32_t m_tx_tick_count = 0;   // кадров TX в текущем тике (#48 §7)
};

} // namespace v3
