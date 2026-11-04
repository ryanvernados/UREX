#ifndef RUNE_UART_H
#define RUNE_UART_H
#include "main.h"
void Rune_UART_Init(void);
int Rune_UART_Line(char *out, unsigned capacity);
int Rune_UART_Send(const char *text);
#endif
