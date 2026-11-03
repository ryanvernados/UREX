#ifndef MOTOR_TEST_H
#define MOTOR_TEST_H
#include <stdint.h>

/* Match the ID programmed into the C620 (1..8). Speed is before gearbox. */
#define MOTOR_ESC_ID               1U
#define MOTOR_TARGET_ROTOR_RPM     1000
#define MOTOR_CURRENT_LIMIT        2000
#define MOTOR_STARTUP_DELAY_MS     2000U
#define MOTOR_FEEDBACK_TIMEOUT_MS  100U
#define MOTOR_SPEED_RAMP_RPM_S     500.0f
#define MOTOR_SPEED_KP             2.0f
#define MOTOR_SPEED_KI             1.0f
#if MOTOR_ESC_ID < 1 || MOTOR_ESC_ID > 8
#error "MOTOR_ESC_ID must be 1..8"
#endif
#if MOTOR_CURRENT_LIMIT < 1 || MOTOR_CURRENT_LIMIT > 16384
#error "MOTOR_CURRENT_LIMIT must be 1..16384"
#endif

typedef struct {
    uint16_t angle;
    int16_t rotor_rpm;
    int16_t measured_current;
    uint8_t temperature_c;
    uint8_t online;
    uint32_t feedback_count;
    uint32_t last_feedback_ms;
    int16_t commanded_current;
    uint32_t tx_count;
    uint32_t tx_failures;
    uint32_t can_error;
} MotorTestStatus;

/* Live Expressions: set enabled=0 to command zero current (coast). */
extern volatile MotorTestStatus motor_status;
extern volatile uint8_t motor_enabled;
extern volatile int32_t motor_target_rpm;
void MotorTest_Init(void);
void MotorTest_Update(void);
#endif
