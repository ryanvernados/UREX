#include "motor_test.h"
#include "can.h"

volatile MotorTestStatus motor_status;
volatile uint8_t motor_enabled = 0U;
volatile int32_t motor_target_rpm = 0;
static uint32_t started_ms;
static uint32_t previous_ms;
static float integral;
static float ramp_rpm;

static float clamp(float value, float limit)
{
    return value > limit ? limit : (value < -limit ? -limit : value);
}

void MotorTest_Init(void)
{
    CAN_FilterTypeDef filter = {0};
    /* Exact standard data-frame ID; reject extended and remote frames. */
    filter.FilterBank = 0;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (0x200U + MOTOR_ESC_ID) << 5;
    filter.FilterMaskIdHigh = 0xFFE0U;
    filter.FilterMaskIdLow = 0x0006U;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14;
    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK ||
        HAL_CAN_Start(&hcan1) != HAL_OK) { Error_Handler(); }
    started_ms = HAL_GetTick();
    previous_ms = started_ms;
}

void MotorTest_Update(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - previous_ms;
    if (elapsed == 0U) { return; }
    previous_ms = now;
    /* Drain FIFO before calculating a command. No RTOS or CAN ISR. */
    for (unsigned n = 0; n < 16U && HAL_CAN_GetRxFifoFillLevel(&hcan1, CAN_RX_FIFO0); ++n) {
        CAN_RxHeaderTypeDef rx;
        uint8_t data[8];
        if (HAL_CAN_GetRxMessage(&hcan1, CAN_RX_FIFO0, &rx, data) != HAL_OK) { break; }
        if (rx.IDE != CAN_ID_STD || rx.RTR != CAN_RTR_DATA ||
            rx.StdId != 0x200U + MOTOR_ESC_ID || rx.DLC != 8U) { continue; }
        motor_status.angle = ((uint16_t)data[0] << 8) | data[1];
        motor_status.rotor_rpm = (int16_t)(((uint16_t)data[2] << 8) | data[3]);
        motor_status.measured_current = (int16_t)(((uint16_t)data[4] << 8) | data[5]);
        motor_status.temperature_c = data[6];
        motor_status.last_feedback_ms = now;
        motor_status.feedback_count++;
    }
    motor_status.can_error = HAL_CAN_GetError(&hcan1);
    motor_status.online = motor_status.feedback_count != 0U &&
        (now - motor_status.last_feedback_ms) <= MOTOR_FEEDBACK_TIMEOUT_MS &&
        (hcan1.Instance->ESR & CAN_ESR_BOFF) == 0U;

    int16_t command = 0;
    if (motor_enabled && motor_target_rpm != 0 && motor_status.online &&
        now - started_ms >= MOTOR_STARTUP_DELAY_MS && elapsed <= 10U) {
        float dt = (float)elapsed * 0.001f;
        float target = clamp((float)motor_target_rpm, 5000.0f);
        float step = MOTOR_SPEED_RAMP_RPM_S * dt;
        ramp_rpm += clamp(target - ramp_rpm, step);
        float error = ramp_rpm - motor_status.rotor_rpm;
        float candidate = clamp(integral + MOTOR_SPEED_KI * error * dt, MOTOR_CURRENT_LIMIT);
        float output = MOTOR_SPEED_KP * error + candidate;
        /* Integrate when unsaturated or unwinding saturation. */
        if ((output <= MOTOR_CURRENT_LIMIT && output >= -MOTOR_CURRENT_LIMIT) ||
            (output > MOTOR_CURRENT_LIMIT && error < 0.0f) ||
            (output < -MOTOR_CURRENT_LIMIT && error > 0.0f)) { integral = candidate; }
        command = (int16_t)clamp(MOTOR_SPEED_KP * error + integral, MOTOR_CURRENT_LIMIT);
    } else {
        integral = 0.0f;
        ramp_rpm = 0.0f;
    }
    motor_status.commanded_current = command;

    /* Replace pending commands with this tick's fresh command. */
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) != 3U) {
        HAL_CAN_AbortTxRequest(&hcan1, CAN_TX_MAILBOX0 | CAN_TX_MAILBOX1 | CAN_TX_MAILBOX2);
    }
    CAN_TxHeaderTypeDef tx = {0};
    uint8_t payload[8] = {0};
    uint32_t mailbox;
    unsigned slot = ((MOTOR_ESC_ID - 1U) % 4U) * 2U;
    tx.StdId = MOTOR_ESC_ID <= 4U ? 0x200U : 0x1FFU;
    tx.IDE = CAN_ID_STD;
    tx.RTR = CAN_RTR_DATA;
    tx.DLC = 8;
    payload[slot] = (uint8_t)((uint16_t)command >> 8);
    payload[slot + 1U] = (uint8_t)command;
    if (HAL_CAN_AddTxMessage(&hcan1, &tx, payload, &mailbox) == HAL_OK) {
        motor_status.tx_count++;
    } else {
        motor_status.tx_failures++;
    }
}
