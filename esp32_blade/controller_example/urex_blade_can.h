#ifndef UREX_BLADE_CAN_H
#define UREX_BLADE_CAN_H
#include "main.h"

enum { UREX_BLADE_OFF, UREX_BLADE_IDLE, UREX_BLADE_TARGET, UREX_BLADE_HIT };
typedef struct {
    uint8_t online, sequence, state_flags, ring;
    uint16_t switch_mask, hit_count;
    uint32_t received_ms;
} UrexBladeStatus;
extern UrexBladeStatus urex_blades[5];
HAL_StatusTypeDef UrexBladeCAN_Init(void);
void UrexBladeCAN_Update(void);
// A call advances this blade's sequence, even if mode is unchanged.
// Call ONCE to change a setting/rearm, not every iteration or heartbeat.
void UrexBladeCAN_Command(uint8_t blade, uint8_t mode, uint8_t brightness,
                          uint8_t red, uint8_t green, uint8_t blue);
#endif
