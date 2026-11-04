#include "rune_app.h"
#include <string.h>
#if UREX_BLADE_ID != 0
/* All pins below are accessible on Type C connectors, see setup guide. */
static GPIO_TypeDef *const ring_ports[10] = {GPIOB,GPIOB,GPIOB,GPIOB,GPIOF,GPIOF,GPIOG,GPIOG,GPIOA,GPIOB};
static const uint16_t ring_pins[10] = {GPIO_PIN_12,GPIO_PIN_13,GPIO_PIN_14,GPIO_PIN_15,GPIO_PIN_1,GPIO_PIN_0,GPIO_PIN_14,GPIO_PIN_9,GPIO_PIN_9,GPIO_PIN_7};
static GPIO_TypeDef *const led_ports[5] = {GPIOE,GPIOE,GPIOE,GPIOE,GPIOC};
static const uint16_t led_pins[5] = {GPIO_PIN_9,GPIO_PIN_11,GPIO_PIN_13,GPIO_PIN_14,GPIO_PIN_6};
static const uint16_t led_counts[5] = BLADE_LED_COUNTS;
static volatile uint8_t initialized, mode, pending_ring;
static volatile uint16_t switch_mask;
static uint8_t command[8], have_command, last_ring, brightness, red, green, blue;
static uint16_t hits;
static uint32_t boot_id, command_at;
static DMA_HandleTypeDef led_dma;
static volatile uint8_t dma_busy;
static uint8_t led_section;
/* Timer update DMA drives GPIO BSRR at 2.4MHz, 3 phases per WS2812 bit.
 * No bit-banging or disabled interrupts while the chain transmits. */
static uint32_t waveform[BLADE_MAX_LEDS_PER_CHAIN * 24U * 3U + 2048U];
static void led_done(DMA_HandleTypeDef *dma)
{
    (void)dma; TIM1->CR1 = 0; TIM1->DIER = 0;
    for (unsigned i = 0; i < 5; ++i) { led_ports[i]->BSRR = (uint32_t)led_pins[i] << 16; }
    dma_busy = 0;
}
void DMA2_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&led_dma); }
static void led_update(uint32_t now)
{
    static uint32_t last_frame;
    if (dma_busy || (led_section == 0 && now - last_frame < 50)) { return; }
    if (led_section == 0) { last_frame = now; }
    unsigned section = led_section;
    uint16_t pin = led_pins[section], count = led_counts[section];
    if (count > BLADE_MAX_LEDS_PER_CHAIN) { Error_Handler(); }
    uint32_t clear = (uint32_t)pin << 16;
    unsigned used = 0;
    for (unsigned i = 0; i < 1024; ++i) { waveform[used++] = clear; }
    unsigned lit = count;
    if (mode == 3 && section == 3) { lit = ((uint32_t)last_ring * count + 9) / 10; }
    for (unsigned pixel = 0; pixel < count; ++pixel) {
        uint8_t scale = mode == 1 ? brightness / 16 : brightness;
        uint32_t color = 0;
        if (mode && pixel < lit) {
            color = ((uint32_t)(green * scale / 255) << 16) |
                    ((uint32_t)(red * scale / 255) << 8) | (blue * scale / 255);
        }
        for (int bit = 23; bit >= 0; --bit) {
            waveform[used++] = pin;
            waveform[used++] = color & (1UL << bit) ? 0 : clear;
            waveform[used++] = clear;
        }
    }
    for (unsigned i = 0; i < 1024; ++i) { waveform[used++] = clear; }
    TIM1->CR1 = 0; TIM1->DIER = 0; TIM1->CNT = 0;
    TIM1->EGR = TIM_EGR_UG; TIM1->SR = 0;
    dma_busy = 1;
    if (HAL_DMA_Start_IT(&led_dma, (uint32_t)waveform, (uint32_t)&led_ports[section]->BSRR, used) != HAL_OK) { Error_Handler(); }
    TIM1->DIER = TIM_DIER_UDE; TIM1->CR1 = TIM_CR1_CEN;
    led_section = (led_section + 1) % 5;
}
void Rune_Tick1ms(void)
{
    if (!initialized) { return; }
    static uint16_t previous, stable, collected;
    static uint32_t changed[10], released_at, collection_at, last_hit;
    static uint8_t released, ready, collecting;
    uint32_t now = HAL_GetTick(); uint16_t raw = 0;
    for (unsigned i = 0; i < 10; ++i) {
        uint16_t bit = 1U << i;
        if (!(ring_ports[i]->IDR & ring_pins[i])) { raw |= bit; }
        if ((raw ^ previous) & bit) { changed[i] = now; }
        if (now - changed[i] >= 2) { stable = (stable & ~bit) | (raw & bit); }
    }
    previous = raw; switch_mask = stable;
    if (!raw && !stable) {
        if (!released) { released = 1; released_at = now; }
        if (!collecting && now - released_at >= 25 && now - last_hit >= 100) { ready = 1; }
    } else { released = 0; }
    if (ready && stable) {
        ready = 0;
        if (mode == 2) { collecting = 1; collection_at = now; collected = stable; }
    }
    if (collecting) {
        collected |= stable;
        if (mode != 2) { collecting = 0; }
        else if (now - collection_at >= 15) {
            uint8_t ring = 0;
            for (unsigned i = 0; i < 10; ++i) { if (collected & (1U << i)) { ring = i + 1; } }
            pending_ring = ring;
            /* Stop sampling another hit before the main loop consumes this one. */
            mode = 3; collecting = 0; released = 0; last_hit = now;
        }
    }
}
void Rune_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE(); __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_INPUT; gpio.Pull = GPIO_PULLUP;
    for (unsigned i = 0; i < 10; ++i) { gpio.Pin = ring_pins[i]; HAL_GPIO_Init(ring_ports[i], &gpio); }
    gpio.Mode = GPIO_MODE_OUTPUT_PP; gpio.Pull = GPIO_NOPULL; gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    for (unsigned i = 0; i < 5; ++i) {
        led_ports[i]->BSRR = (uint32_t)led_pins[i] << 16;
        gpio.Pin = led_pins[i]; HAL_GPIO_Init(led_ports[i], &gpio);
        if (!led_counts[i] || led_counts[i] > BLADE_MAX_LEDS_PER_CHAIN) { Error_Handler(); }
    }
    __HAL_RCC_TIM1_CLK_ENABLE(); __HAL_RCC_DMA2_CLK_ENABLE();
    TIM1->PSC = 0; TIM1->ARR = 69; /* 168MHz / 70 = 2.4MHz */
    led_dma.Instance = DMA2_Stream5;
    led_dma.Init.Channel = DMA_CHANNEL_6; /* TIM1_UP */
    led_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    led_dma.Init.PeriphInc = DMA_PINC_DISABLE; led_dma.Init.MemInc = DMA_MINC_ENABLE;
    led_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD; led_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    led_dma.Init.Mode = DMA_NORMAL; led_dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    led_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&led_dma) != HAL_OK) { Error_Handler(); }
    led_dma.XferCpltCallback = led_done;
    led_dma.XferErrorCallback = led_done;
    HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 5, 0); HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
    boot_id = Rune_Random32(); Rune_CAN_Init(); initialized = 1;
}
void Rune_Update(void)
{
    uint32_t now = HAL_GetTick();
    /* Consume a hit before accepting any new host command. */
    __disable_irq(); uint8_t ring = pending_ring; pending_ring = 0; __enable_irq();
    if (ring && have_command) { last_ring = ring; ++hits; }
    for (unsigned n = 0; n < 32 && HAL_CAN_GetRxFifoFillLevel(&rune_can, CAN_RX_FIFO0); ++n) {
        CAN_RxHeaderTypeDef rx; uint8_t d[8];
        if (HAL_CAN_GetRxMessage(&rune_can, CAN_RX_FIFO0, &rx, d) != HAL_OK) { break; }
        if (rx.IDE != CAN_ID_STD || rx.RTR != CAN_RTR_DATA || rx.DLC != 8 ||
            rx.StdId != 0x300 + UREX_BLADE_ID || d[0] != 1 || d[2] > 3 || d[7]) { continue; }
        if (have_command && d[1] == command[1]) {
            if (!memcmp(d, command, 8)) { command_at = now; }
            continue;
        }
        __disable_irq();
        memcpy(command, d, 8); have_command = 1; command_at = now;
        mode = d[2]; pending_ring = 0;
        if (mode != 3) { last_ring = 0; }
        __enable_irq();
        brightness = d[3] > BLADE_BRIGHTNESS_LIMIT ? BLADE_BRIGHTNESS_LIMIT : d[3];
        red = d[4]; green = d[5]; blue = d[6];
    }
    if (now - command_at >= 1000 || (rune_can.Instance->ESR & CAN_ESR_BOFF)) { mode = 0; have_command = 0; }
    static uint32_t status_at, hello_at;
    if (now - hello_at >= 250) {
        uint8_t d[8] = {1, boot_id, boot_id >> 8, boot_id >> 16, boot_id >> 24, UREX_BLADE_ID, 1, 0};
        if (Rune_CAN_Send(0x3C0 + UREX_BLADE_ID, d) == HAL_OK) { hello_at = now; }
    }
    if (now - status_at >= 50) {
        uint8_t d[8] = {1, command[1], mode | (have_command ? 0 : 0x10), last_ring,
                        switch_mask, switch_mask >> 8, hits, hits >> 8};
        if (Rune_CAN_Send(0x380 + UREX_BLADE_ID, d) == HAL_OK) { status_at = now; }
    }
    led_update(now);
}
#endif
