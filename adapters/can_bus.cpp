// CAN bus adapter implementation (design docs/safety-authority-design-v3.md
// sections 2.4, 3.5; ticket #71). Uses HAL CAN (bring-up proven pattern,
// bench/bringup/can_loopback.cpp). Arduino/HAL confined here.
#include "adapters/can_bus.h"

#include <Arduino.h>
#include <cstdint>

#include "stm32f4xx_hal.h"

#include "domain/can_contract.h"
#include "platform/monotonic.h"

namespace v3
{
namespace
{

constexpr std::uint32_t kTxBudgetPerTick = 16u; // #48 §7
constexpr std::uint32_t kRxBudgetPerTick = 64u; // #48 §7
constexpr std::uint32_t kTickMs = 10u;          // T_step window for the TX budget reset

CAN_HandleTypeDef g_hcan;

bool can_init(bool loopback)
{
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // CAN1_RX = PB8, CAN1_TX = PB9 (AF9) - netlist contract, bench record #73.
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &gpio);

    // 500 kbit/s from APB1 42 MHz: prescaler 4, 21 tq/bit (sync 1 + BS1 13 + BS2 7).
    g_hcan.Instance = CAN1;
    g_hcan.Init.Prescaler = 4;
    g_hcan.Init.Mode = loopback ? CAN_MODE_LOOPBACK : CAN_MODE_NORMAL;
    g_hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    g_hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
    g_hcan.Init.TimeSeg2 = CAN_BS2_7TQ;
    g_hcan.Init.TimeTriggeredMode = DISABLE;
    g_hcan.Init.AutoBusOff = DISABLE; // manual bus-off recovery (recover_bus_off)
    g_hcan.Init.AutoWakeUp = DISABLE;
    g_hcan.Init.AutoRetransmission = ENABLE;
    g_hcan.Init.ReceiveFifoLocked = DISABLE;
    g_hcan.Init.TransmitFifoPriority = DISABLE; // mailbox-number priority (0 highest)
    if (HAL_CAN_Init(&g_hcan) != HAL_OK)
    {
        return false;
    }

    // Accept-all filter (Phase 1: no RX consumers; bounded drain + drop policy).
    CAN_FilterTypeDef filter{};
    filter.FilterIdHigh = 0;
    filter.FilterIdLow = 0;
    filter.FilterMaskIdHigh = 0;
    filter.FilterMaskIdLow = 0;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(&g_hcan, &filter) != HAL_OK)
    {
        return false;
    }

    return HAL_CAN_Start(&g_hcan) == HAL_OK;
}

} // namespace

void CanBus::init(bool loopback, SafetyDiag* diag)
{
    m_diag = diag;
    m_budget_reset_ms = 0;
    m_tx_tick_count = 0;
    (void)can_init(loopback);
    if (m_diag != nullptr)
    {
        m_diag->can_state = error_state();
    }
}

bool CanBus::tx(const CanFrame& f)
{
    // Per-tick TX budget (#48 §7): window-reset on T_step boundary.
    const std::uint64_t now = monotonic::now_ms();
    if (now >= m_budget_reset_ms + kTickMs)
    {
        m_tx_tick_count = 0;
        m_budget_reset_ms = now;
    }
    if (m_tx_tick_count >= kTxBudgetPerTick)
    {
        if (m_diag != nullptr)
        {
            ++m_diag->can_tx_dropped;
        }
        return false;
    }

    CAN_TxHeaderTypeDef hdr{};
    hdr.ExtId = f.id;
    hdr.IDE = CAN_ID_EXT;
    hdr.RTR = CAN_RTR_DATA;
    hdr.DLC = f.len > 8u ? 8u : f.len;
    std::uint8_t data[8] = {};
    for (std::uint32_t i = 0; i < hdr.DLC; ++i)
    {
        data[i] = f.data[i];
    }
    std::uint32_t mailbox = 0;
    // HAL_CAN_AddTxMessage auto-selects the lowest free mailbox. Returns HAL_ERROR
    // when all are pending (transient; bounded, never blocks).
    if (HAL_CAN_AddTxMessage(&g_hcan, &hdr, data, &mailbox) != HAL_OK)
    {
        if (m_diag != nullptr)
        {
            ++m_diag->can_tx_dropped;
        }
        return false;
    }
    ++m_tx_tick_count;
    if (m_diag != nullptr)
    {
        ++m_diag->can_tx_count;
    }
    return true;
}

std::uint32_t CanBus::rx_drain(CanFrame* out, std::uint32_t budget)
{
    if (budget > kRxBudgetPerTick)
    {
        budget = kRxBudgetPerTick;
    }
    std::uint32_t n = 0;
    CAN_RxHeaderTypeDef hdr;
    std::uint8_t data[8] = {};
    while (n < budget && HAL_CAN_GetRxMessage(&g_hcan, CAN_RX_FIFO0, &hdr, data) == HAL_OK)
    {
        out[n].id = hdr.ExtId;
        out[n].len = hdr.DLC > 8u ? 8u : hdr.DLC;
        for (std::uint32_t i = 0; i < out[n].len; ++i)
        {
            out[n].data[i] = data[i];
        }
        ++n;
    }
    // RX-overrun (обязательство #13, #43 §4): аппаратное переполнение FIFO0 - drop +
    // счётчик (событие - у Observability #72). FOVR0 clear-on-write-1 (CAN_RF0R).
    if (CAN1->RF0R & CAN_RF0R_FOVR0)
    {
        if (m_diag != nullptr)
        {
            ++m_diag->can_rx_dropped;
        }
        CAN1->RF0R = CAN_RF0R_FOVR0;
    }
    return n;
}

void CanBus::force_stop_tx()
{
    // Force-stop preempts pending normal TX: ABRQ all mailboxes, then BOUNDED-wait
    // for the TME (transmit mailbox empty) bits - ABRQ is async; AddTxMessage before
    // the mailboxes free would return HAL_ERROR and the safety frame would vanish.
    CAN1->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;
    for (std::uint32_t i = 0; i < 1000u &&
         (CAN1->TSR & (CAN_TSR_TME0 | CAN_TSR_TME1 | CAN_TSR_TME2)) == 0u; ++i)
    {
    }

    CAN_TxHeaderTypeDef hdr{};
    hdr.ExtId = v3::safety::kForceStopFrame.id;
    hdr.IDE = CAN_ID_EXT;
    hdr.RTR = CAN_RTR_DATA;
    hdr.DLC = v3::safety::kForceStopFrame.len;
    std::uint8_t data[8] = {};
    for (std::uint32_t i = 0; i < hdr.DLC; ++i)
    {
        data[i] = v3::safety::kForceStopFrame.data[i];
    }
    std::uint32_t mailbox = 0;
    // Best-effort on a failing bus: failure is observable (can_tx_dropped); the real
    // safety net is the per-device commissioning fail-safe of the drives (Q7.1 A).
    if (HAL_CAN_AddTxMessage(&g_hcan, &hdr, data, &mailbox) == HAL_OK)
    {
        if (m_diag != nullptr)
        {
            ++m_diag->can_tx_count;
            m_diag->record_frame(2u, v3::safety::kForceStopFrame, monotonic::now_ms());
        }
    }
    else if (m_diag != nullptr)
    {
        ++m_diag->can_tx_dropped;
    }
}

CanErrorState CanBus::error_state() const
{
    const std::uint32_t esr = CAN1->ESR;
    if (esr & CAN_ESR_BOFF)
    {
        return CanErrorState::BusOff;
    }
    if (esr & CAN_ESR_EPVF)
    {
        return CanErrorState::ErrorPassive;
    }
    return CanErrorState::Active;
}

void CanBus::recover_bus_off()
{
    // Manual bus-off recovery (AutoBusOff = DISABLE): request init mode and wait for
    // the INIT acknowledgment (CAN_MSR_INAK, NOT SLAK - sleep ack), then leave init
    // mode (software re-integration request); re-integration completes after 128
    // occurrences of 11 recessive bits (RM0090 §32.7). Bounded. Fault stays until
    // explicit reset (HZ-03, #45).
    if ((CAN1->MSR & CAN_MSR_INAK) == 0u)
    {
        CAN1->MCR |= CAN_MCR_INRQ;
        for (std::uint32_t i = 0; i < 1000u && (CAN1->MSR & CAN_MSR_INAK) == 0u; ++i)
        {
        }
    }
    CAN1->MCR &= ~static_cast<std::uint32_t>(CAN_MCR_INRQ);
    for (std::uint32_t i = 0; i < 1000u && (CAN1->MSR & CAN_MSR_INAK) != 0u; ++i)
    {
    }
    if (m_diag != nullptr)
    {
        ++m_diag->can_bus_off_recoveries;
        m_diag->can_state = error_state();
    }
}

} // namespace v3
