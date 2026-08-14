// UART TX adapter (design docs/observability-design-v3.md sections 3.4, 5.1,
// 6; ticket #72). USART1 PA9 TX / PA10 RX, 230400 8E1 - the only network_bridge
// UART contract on the bench (runner #65; V1 display path XT22).
//
// Never blocks (obligation #12, #43 section 6): tx() copies into a 256 B ring
// and returns false when the ring has no room; the Sink applies defer/drop
// policy (defer-on-backpressure, #49 section 10). The TXE interrupt moves one
// byte ring->TDR per fire and disables itself when the ring is empty (rule R2:
// ISR performs no policy, no counters, no events).
//
// RX (PA10) is NOT drained in #72 - the transport adapter (#75) owns ingress;
// the USART RX path stays disabled (no ORE risk) until then.
#pragma once

#include <cstdint>

#include "domain/ports.h"

namespace v3
{

class UartBridge : public UartPort
{
  public:
    static constexpr std::uint32_t RingSize = 256; // design §0.1 (owner decision)

    void init(); // USART1 230400 8E1, TXE interrupt on, RX disabled
    std::uint32_t tx_bytes_available() const override;
    bool tx(const std::uint8_t* data, std::uint32_t len) override;

    // Foreground drain hook (test/observability): not used by production
    // (the TXE ISR drains); kept for the host ring simulation tests.
    void service();

  private:
    static void uart1_isr(); // target ISR: ring->TDR move only (rule R2)
    static UartBridge* s_self; // single-instance trampoline for the ISR
    volatile std::uint8_t m_ring[RingSize] = {};
    volatile std::uint32_t m_head = 0; // ISR consumes
    volatile std::uint32_t m_tail = 0; // foreground produces
    volatile bool m_txe_enabled = false;
};

} // namespace v3
