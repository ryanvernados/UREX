/* Optional Type C example. Not compiled into the existing motor application.
 * Copy to Core only after reviewing README.md and disabling motor autostart.
 * Uses CAN2 PB5/PB6 at 500 kbit/s; existing CAN1 motor bus stays at 1 Mbit/s.
 */
#include "urex_blade_can.h"

static CAN_HandleTypeDef blade_can;
UrexBladeStatus urex_blades[5];
static uint8_t commands[5][8];
static uint32_t last_send;
static uint8_t next_blade;

HAL_StatusTypeDef UrexBladeCAN_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_CAN1_CLK_ENABLE(); /* CAN2 depends on CAN1 clock. */
    __HAL_RCC_CAN2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF9_CAN2;
    HAL_GPIO_Init(GPIOB, &gpio);
    blade_can.Instance = CAN2;
    blade_can.Init.Prescaler = 6; /* 42 MHz / (6*(1+10+3)) = 500 kbit/s. */
    blade_can.Init.Mode = CAN_MODE_NORMAL;
    blade_can.Init.SyncJumpWidth = CAN_SJW_1TQ;
    blade_can.Init.TimeSeg1 = CAN_BS1_10TQ;
    blade_can.Init.TimeSeg2 = CAN_BS2_3TQ;
    blade_can.Init.AutoBusOff = ENABLE;
    blade_can.Init.AutoRetransmission = DISABLE;
    blade_can.Init.TransmitFifoPriority = ENABLE;
    if (HAL_CAN_Init(&blade_can) != HAL_OK) { return HAL_ERROR; }
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = 14;
    filter.SlaveStartFilterBank = 14;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = 0x380U << 5;
    filter.FilterMaskIdHigh = 0x7F0U << 5;
    filter.FilterMaskIdLow = 6;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    if (HAL_CAN_ConfigFilter(&blade_can, &filter) != HAL_OK ||
        HAL_CAN_Start(&blade_can) != HAL_OK) { return HAL_ERROR; }
    for (unsigned i = 0; i < 5; ++i) {
        commands[i][0] = 1;
        commands[i][2] = UREX_BLADE_OFF;
    }
    last_send = HAL_GetTick();
    return HAL_OK;
}

void UrexBladeCAN_Command(uint8_t blade, uint8_t mode, uint8_t brightness,
                          uint8_t red, uint8_t green, uint8_t blue)
{
    if (blade < 1 || blade > 5 || mode > UREX_BLADE_HIT) { return; }
    uint8_t *d = commands[blade - 1];
    ++d[1];
    d[2] = mode; d[3] = brightness; d[4] = red; d[5] = green; d[6] = blue;
}

void UrexBladeCAN_Update(void)
{
    uint32_t now = HAL_GetTick();
    for (unsigned n = 0; n < 16 && HAL_CAN_GetRxFifoFillLevel(&blade_can, CAN_RX_FIFO0); ++n) {
        CAN_RxHeaderTypeDef rx;
        uint8_t d[8];
        if (HAL_CAN_GetRxMessage(&blade_can, CAN_RX_FIFO0, &rx, d) != HAL_OK) { break; }
        if (rx.IDE != CAN_ID_STD || rx.RTR != CAN_RTR_DATA || rx.DLC != 8 ||
            rx.StdId < 0x381 || rx.StdId > 0x385 || d[0] != 1) { continue; }
        UrexBladeStatus *s = &urex_blades[rx.StdId - 0x381];
        s->received_ms = now; s->sequence = d[1]; s->state_flags = d[2]; s->ring = d[3];
        s->switch_mask = d[4] | ((uint16_t)d[5] << 8);
        s->hit_count = d[6] | ((uint16_t)d[7] << 8);
    }
    for (unsigned i = 0; i < 5; ++i) {
        // Status can be received in OFF/timeout state; inspect state_flags separately.
        urex_blades[i].online = urex_blades[i].received_ms != 0 &&
                               now - urex_blades[i].received_ms < 500;
    }
    // One board every 20ms: each blade receives the same heartbeat every 100ms.
    if (now - last_send < 20 || HAL_CAN_GetTxMailboxesFreeLevel(&blade_can) == 0) { return; }
    last_send = now;
    CAN_TxHeaderTypeDef tx = {0};
    uint32_t mailbox;
    tx.StdId = 0x301 + next_blade;
    tx.IDE = CAN_ID_STD; tx.RTR = CAN_RTR_DATA; tx.DLC = 8;
    if (HAL_CAN_AddTxMessage(&blade_can, &tx, commands[next_blade], &mailbox) == HAL_OK) {
        next_blade = (next_blade + 1) % 5;
    }
}
