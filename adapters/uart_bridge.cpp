// UART TX adapter implementation (design docs/observability-design-v3.md
// section 6; ticket #72). Target-only (Arduino Core). Single ISR: USART1 TXE.
#include "adapters/uart_bridge.h"

#ifdef ARDUINO

#include <Arduino.h>

namespace v3
{

UartBridge* UartBridge::s_self = nullptr;

// USART1 TXE interrupt: move one byte ring->TDR; disable TXE when empty.
// Rule R2 (#43 section 3.2): nothing but the byte move. No policy, no
// counters, no events from the ISR.
void UartBridge::uart1_isr()
{
    if (s_self == nullptr)
    {
        return;
    }
    USART_TypeDef* u = USART1;
    if ((u->SR & USART_SR_TXE) == 0u)
    {
        return;
    }
    if (s_self->m_head != s_self->m_tail)
    {
        u->DR = s_self->m_ring[s_self->m_head];
        s_self->m_head = (s_self->m_head + 1u) % RingSize;
    }
    else
    {
        u->CR1 &= ~USART_CR1_TXEIE; // ring empty: stop interrupts
        s_self->m_txe_enabled = false;
    }
}

void UartBridge::isr_dispatch()
{
    uart1_isr(); // single instance: static trampoline into the member path
}

void UartBridge::init()
{
    s_self = this;
    m_head = 0;
    m_tail = 0;
    m_txe_enabled = false;

    // USART1 on PA9 (TX) / PA10 (RX), 230400 8E1 (V1 display path, XT22).
    // We drive the USART directly, NOT through the Arduino Serial1/HAL_UART
    // stack (owner decision #72 0.1: own driver for full never-block/ring
    // control). The core's USART1_IRQHandler dispatches to
    // HAL_UART_IRQHandler(uart_handlers[UART1_INDEX]) which is NULL unless
    // Serial1.begin() ran - so the core Serial layer is compiled out
    // (-D HAL_UART_MODULE_ONLY in platformio.ini) and the strong symbol
    // below claims the (weak startup) vector slot instead.
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    GPIOA->MODER = (GPIOA->MODER & ~(3u << 18)) | (2u << 18); // PA9 AF
    GPIOA->MODER = (GPIOA->MODER & ~(3u << 20)) | (2u << 20); // PA10 AF
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFu << 4)) | (7u << 4);  // PA9 AF7 = USART1
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~(0xFu << 8)) | (7u << 8);  // PA10 AF7 = USART1

    // 230400 8E1, oversampling 16: USART1 is on APB2; SYSCLK 168 MHz with
    // PPRE2 /2 gives PCLK2 = 84 MHz (measured RCC_CFGR 0x940A on the bench,
    // L4 observability-uart leg 2026-09-01). BRR = 84e6 / 230400 = 364.58
    // => 365 (0.11% error). 8 data bits + even parity needs M=1: with
    // PCE=1 the word length bits (M=1) carry 8 data + 1 parity = 9-bit frame.
    USART1->CR1 = 0;                        // reset
    USART1->BRR = 365;
    USART1->CR1 = USART_CR1_UE | USART_CR1_M | USART_CR1_TE;
    USART1->CR2 = USART_CR2_STOP_1;         // 1 stop bit
    USART1->CR1 |= USART_CR1_PCE;           // parity even (PS=0)

    NVIC_SetPriority(USART1_IRQn, 2); // below TIM2 (0) - no inversion
    NVIC_EnableIRQ(USART1_IRQn);
}

// Strong symbol: claims the weak startup slot for USART1. The core
// uart.c handler (HAL dispatch with a NULL uart_handlers[] entry) is
// compiled out by -D HAL_UART_MODULE_ONLY; without it the first TXE
// interrupt faulted to HardFault (L4 evidence 2026-09-01: PC in
// HAL_UART_IRQHandler, CFSR 0x8200, 0 bytes on the line).
extern "C" void USART1_IRQHandler(void)
{
    v3::UartBridge::isr_dispatch();
}


std::uint32_t UartBridge::tx_bytes_available() const
{
    const std::uint32_t head = m_head;
    const std::uint32_t tail = m_tail;
    if (tail >= head)
    {
        return RingSize - (tail - head) - 1u; // one slot reserved (full vs empty)
    }
    return head - tail - 1u;
}

bool UartBridge::tx(const std::uint8_t* data, std::uint32_t len)
{
    if (len >= RingSize)
    {
        return false; // never fits
    }
    if (len > tx_bytes_available())
    {
        return false; // never blocks
    }
    for (std::uint32_t i = 0; i < len; ++i)
    {
        m_ring[m_tail] = data[i];
        m_tail = (m_tail + 1u) % RingSize;
    }
    if (!m_txe_enabled)
    {
        m_txe_enabled = true;
        USART1->CR1 |= USART_CR1_TXEIE; // ISR drains the ring
    }
    return true;
}

void UartBridge::service()
{
    // Foreground drain (host ring simulation / diagnostics); the target uses
    // the TXE ISR instead. No-op when the ISR path owns the ring.
}

} // namespace v3

#endif // ARDUINO
