#include "rune_uart.h"
#include <string.h>
/* USART6 raw interrupt ring buffers. No blocking UART writes in motor loop. */
static volatile uint16_t rx_head, rx_tail, tx_head, tx_tail;
static uint8_t rx[512], tx[4096];
static volatile uint8_t rx_overflow;
static char line[320];
static unsigned used;
static uint8_t dropping;
static uint16_t crc16(const char *s)
{
    uint16_t crc = 0xffff;
    while (*s) {
        crc ^= (uint8_t)*s++ << 8;
        for (unsigned n = 0; n < 8; ++n) { crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1; }
    }
    return crc;
}
void Rune_UART_Init(void)
{
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_USART6_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_AF_PP; gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOG, &gpio);
    USART6->BRR = (HAL_RCC_GetPCLK2Freq() + 57600U) / 115200U;
    USART6->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART6_IRQn, 6, 0); HAL_NVIC_EnableIRQ(USART6_IRQn);
}
void USART6_IRQHandler(void)
{
    uint32_t status = USART6->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        uint8_t value = USART6->DR;
        uint16_t next = (rx_head + 1U) % sizeof(rx);
        if ((status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) || next == rx_tail) { rx_overflow = 1; }
        else { rx[rx_head] = value; rx_head = next; }
    }
    if ((status & USART_SR_TXE) && (USART6->CR1 & USART_CR1_TXEIE)) {
        if (tx_tail == tx_head) { USART6->CR1 &= ~USART_CR1_TXEIE; }
        else { USART6->DR = tx[tx_tail]; tx_tail = (tx_tail + 1U) % sizeof(tx); }
    }
}
int Rune_UART_Line(char *out, unsigned capacity)
{
    if (rx_overflow) { dropping = 1; used = 0; rx_overflow = 0; }
    while (rx_tail != rx_head) {
        char c = rx[rx_tail]; rx_tail = (rx_tail + 1U) % sizeof(rx);
        if (c == '\n') {
            if (dropping) { dropping = 0; used = 0; continue; }
            line[used] = 0; used = 0;
            char *star = strrchr(line, '*');
            if (!star || strlen(star + 1) != 4) { continue; }
            unsigned expected = 0;
            for (unsigned i = 1; i <= 4; ++i) {
                char h = star[i];
                if (h >= '0' && h <= '9') { expected = expected * 16 + h - '0'; }
                else if (h >= 'A' && h <= 'F') { expected = expected * 16 + h - 'A' + 10; }
                else { expected = 0x10000; break; }
            }
            *star = 0;
            if (crc16(line) == expected && strlen(line) < capacity) { strcpy(out, line); return 1; }
        } else if (!dropping && c != '\r') {
            if (used + 1 < sizeof(line)) { line[used++] = c; }
            else { dropping = 1; used = 0; }
        }
    }
    return 0;
}
int Rune_UART_Send(const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned length = strlen(text);
    unsigned free_bytes = (tx_tail + sizeof(tx) - tx_head - 1U) % sizeof(tx);
    if (length + 6U > free_bytes) { return 0; }
    uint16_t crc = crc16(text);
    for (unsigned i = 0; i < length; ++i) { tx[tx_head] = text[i]; tx_head = (tx_head + 1U) % sizeof(tx); }
    tx[tx_head] = '*'; tx_head = (tx_head + 1U) % sizeof(tx);
    for (int shift = 12; shift >= 0; shift -= 4) { tx[tx_head] = hex[(crc >> shift) & 15]; tx_head = (tx_head + 1U) % sizeof(tx); }
    tx[tx_head] = '\n'; tx_head = (tx_head + 1U) % sizeof(tx);
    USART6->CR1 |= USART_CR1_TXEIE;
    return 1;
}
