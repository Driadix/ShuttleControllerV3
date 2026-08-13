// Bring-up CAN loopback diagnostic (ticket #61, D1a, design
// docs/bringup-design-v3.md section 5). NOT the production CAN HAL (that is
// Phase 1: bounded TX/RX, force-stop mailbox): this probe configures CAN1 on
// PB8/PB9 (AF9) per netlist and proves the peripheral via internal LoopBack
// mode (LoopBack never leaves the chip; pin/bus electrical integrity is
// ticket #62).
//
// The result struct is pinned at 0x20010000 (linker section .bram_diag,
// set by -Wl,--section-start in the bringup-can env) and read back by
// bench/bringup/can_loopback_check.py over OpenOCD. UART is disabled in the
// frozen baseline, so diagnostics go through RAM, not the UART.
#include <Arduino.h>
#include <cstdint>

#include "stm32f4xx_hal.h"

namespace
{

constexpr std::uint32_t kMagic = 0xCA11D1A6u; // "CALIDIAG"
constexpr std::uint32_t kFrames = 8u;
constexpr std::uint32_t kTestId = 0x123u;

struct CanDiagResult
{
    std::uint32_t magic;
    std::uint32_t tx_ok;
    std::uint32_t rx_ok;
    std::uint32_t crc_err;
    std::uint32_t done; // 1 when the whole loop completed
};

// Pinned at 0x20010000 by the bringup-can env linker flag
// (--section-start=.bram_diag). Runtime-written RAM; flash_diag.py programs
// firmware-flash.bin (objcopy-stripped flash image) so this RAM section
// never reaches OpenOCD.
__attribute__((section(".bram_diag"))) volatile CanDiagResult g_result;

CAN_HandleTypeDef g_hcan;

bool can_init()
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

    // 500 kbit/s from APB1 42 MHz: prescaler 4, 21 tq/bit (sync 1 + BS1 13 +
    // BS2 7). LoopBack mode - no transceiver, no peer required (physical bus
    // and CAN оснастка are ticket #62).
    g_hcan.Instance = CAN1;
    g_hcan.Init.Prescaler = 4;
    g_hcan.Init.Mode = CAN_MODE_LOOPBACK;
    g_hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    g_hcan.Init.TimeSeg1 = CAN_BS1_13TQ;
    g_hcan.Init.TimeSeg2 = CAN_BS2_7TQ;
    g_hcan.Init.TimeTriggeredMode = DISABLE;
    g_hcan.Init.AutoBusOff = DISABLE;
    g_hcan.Init.AutoWakeUp = DISABLE;
    g_hcan.Init.AutoRetransmission = ENABLE;
    g_hcan.Init.ReceiveFifoLocked = DISABLE;
    g_hcan.Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(&g_hcan) != HAL_OK)
    {
        return false;
    }

    // Accept-all filter (bring-up probe, no filtering needed).
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

// Send one patterned frame and verify the loopback copy. Sequential on
// purpose: the RX FIFO is 3 deep, so frames are drained one at a time.
bool send_and_check(std::uint8_t index)
{
    CAN_TxHeaderTypeDef tx{};
    tx.StdId = kTestId;
    tx.ExtId = 0;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8;
    tx.TransmitGlobalTime = DISABLE;

    std::uint8_t data[8];
    for (std::uint8_t i = 0; i < 8; ++i)
    {
        data[i] = static_cast<std::uint8_t>(0xAAu + static_cast<std::uint32_t>(index) * 8u + i);
    }

    std::uint32_t mailbox = 0;
    if (HAL_CAN_AddTxMessage(&g_hcan, &tx, data, &mailbox) != HAL_OK)
    {
        return false;
    }
    g_result.tx_ok++;

    const std::uint32_t deadline = millis() + 1000;
    while (HAL_CAN_GetRxFifoFillLevel(&g_hcan, CAN_RX_FIFO0) == 0)
    {
        if (millis() > deadline)
        {
            return false;
        }
    }

    CAN_RxHeaderTypeDef rx{};
    std::uint8_t rdata[8];
    if (HAL_CAN_GetRxMessage(&g_hcan, CAN_RX_FIFO0, &rx, rdata) != HAL_OK)
    {
        g_result.crc_err++;
        return false;
    }
    if (rx.StdId != kTestId || rx.DLC != 8)
    {
        g_result.crc_err++;
        return false;
    }
    for (std::uint8_t i = 0; i < 8; ++i)
    {
        if (rdata[i] != data[i])
        {
            g_result.crc_err++;
            return false;
        }
    }
    g_result.rx_ok++;
    return true;
}

} // namespace

void setup()
{
    g_result.magic = kMagic;
    g_result.tx_ok = 0;
    g_result.rx_ok = 0;
    g_result.crc_err = 0;
    g_result.done = 0;

    if (!can_init())
    {
        return; // magic set, done stays 0 -> host reports FAIL
    }
    for (std::uint32_t i = 0; i < kFrames; ++i)
    {
        if (!send_and_check(static_cast<std::uint8_t>(i)))
        {
            return; // partial -> host reports FAIL
        }
    }
    g_result.done = 1;
}

void loop()
{
    for (;;)
    {
        __asm volatile("wfi");
    }
}
