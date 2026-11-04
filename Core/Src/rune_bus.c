#include "rune_app.h"
CAN_HandleTypeDef rune_can;
uint32_t Rune_Random32(void)
{
    __HAL_RCC_RNG_CLK_ENABLE();
    RNG->CR = RNG_CR_RNGEN;
    uint32_t start = HAL_GetTick();
    while (!(RNG->SR & RNG_SR_DRDY)) {
        if (HAL_GetTick() - start > 100) { Error_Handler(); }
    }
    uint32_t value = RNG->DR;
    RNG->CR = 0;
    return value ? value : 1;
}
void Rune_CAN_Init(void)
{
    __HAL_RCC_CAN1_CLK_ENABLE();
    __HAL_RCC_CAN2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Alternate = GPIO_AF9_CAN2;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    rune_can.Instance = CAN2;
    rune_can.Init.Prescaler = 6;
    rune_can.Init.Mode = CAN_MODE_NORMAL;
    rune_can.Init.SyncJumpWidth = CAN_SJW_1TQ;
    rune_can.Init.TimeSeg1 = CAN_BS1_10TQ;
    rune_can.Init.TimeSeg2 = CAN_BS2_3TQ;
    rune_can.Init.AutoBusOff = ENABLE;
    rune_can.Init.AutoRetransmission = DISABLE;
    if (HAL_CAN_Init(&rune_can) != HAL_OK) { Error_Handler(); }
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = 14;
    filter.SlaveStartFilterBank = 14;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterMaskIdLow = 6; /* Standard DATA frames only. */
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(&rune_can, &filter) != HAL_OK ||
        HAL_CAN_Start(&rune_can) != HAL_OK) { Error_Handler(); }
}
HAL_StatusTypeDef Rune_CAN_Send(uint16_t id, const uint8_t data[8])
{
    if (!HAL_CAN_GetTxMailboxesFreeLevel(&rune_can)) { return HAL_BUSY; }
    CAN_TxHeaderTypeDef tx = {0};
    uint32_t mailbox;
    tx.StdId = id; tx.IDE = CAN_ID_STD; tx.RTR = CAN_RTR_DATA; tx.DLC = 8;
    return HAL_CAN_AddTxMessage(&rune_can, &tx, (uint8_t *)data, &mailbox);
}
