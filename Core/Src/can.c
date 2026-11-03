#include "can.h"
CAN_HandleTypeDef hcan1;
void MX_CAN1_Init(void)
{
    hcan1.Instance = CAN1;
    /* APB1=42 MHz: 42 MHz / (3 * (1+10+3)) = 1 Mbit/s. */
    hcan1.Init.Prescaler = 3;
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1 = CAN_BS1_10TQ;
    hcan1.Init.TimeSeg2 = CAN_BS2_3TQ;
    hcan1.Init.TimeTriggeredMode = DISABLE;
    hcan1.Init.AutoBusOff = ENABLE;
    hcan1.Init.AutoWakeUp = DISABLE;
    /* Do not leave an old current command retrying indefinitely. */
    hcan1.Init.AutoRetransmission = DISABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;
    hcan1.Init.TransmitFifoPriority = ENABLE;
    if (HAL_CAN_Init(&hcan1) != HAL_OK) { Error_Handler(); }
}
void HAL_CAN_MspInit(CAN_HandleTypeDef *can)
{
    if (can->Instance == CAN1) {
        GPIO_InitTypeDef gpio = {0};
        __HAL_RCC_CAN1_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        /* Type C: PD0 RX, PD1 TX. Receive is polled. */
        gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF9_CAN1;
        HAL_GPIO_Init(GPIOD, &gpio);
    }
}
