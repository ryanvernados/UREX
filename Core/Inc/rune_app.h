#ifndef RUNE_APP_H
#define RUNE_APP_H
#include "main.h"
#include "rune_config.h"
void Rune_Init(void);
void Rune_Update(void);
void Rune_Tick1ms(void);
uint32_t Rune_Random32(void);
/* Shared CAN2 link; main and all blades use CAN2 at 500 kbit/s. */
extern CAN_HandleTypeDef rune_can;
void Rune_CAN_Init(void);
HAL_StatusTypeDef Rune_CAN_Send(uint16_t id, const uint8_t data[8]);
#endif
