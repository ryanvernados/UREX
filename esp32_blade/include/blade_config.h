#pragma once
#include <Arduino.h>

// ESP32-S3 only. Check these GPIOs against your particular carrier board.
constexpr uint8_t BLADE_ID = 1;  // Change to 1..5, unique on the CAN bus.
constexpr int CAN_TX_PIN = 1;
constexpr int CAN_RX_PIN = 2;
constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;
constexpr uint32_t STATUS_PERIOD_MS = 50;
constexpr uint32_t SERIAL_BENCH_TIMEOUT_MS = 10000;

// Index 0 is ring 1, index 9 is ring 10. Switch closes to GND.
constexpr uint8_t RING_PINS[10] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
constexpr uint32_t SWITCH_DEBOUNCE_MS = 2;
constexpr uint32_t HIT_COLLECTION_MS = 15;
constexpr uint32_t RELEASE_STABLE_MS = 25;
constexpr uint32_t HIT_COOLDOWN_MS = 100;

// Arrow, upper border, lower border, central target, outer border.
constexpr uint8_t LED_PINS[5] = {14, 15, 16, 17, 18};
// PLACEHOLDERS for a small bench test. Replace with your PCB's exact counts.
constexpr uint16_t LED_COUNTS[5] = {30, 30, 30, 30, 30};
constexpr uint8_t MAX_BRIGHTNESS = 64; // Hard cap even if host asks for 255.
constexpr uint32_t LED_FRAME_MS = 40;

static_assert(BLADE_ID >= 1 && BLADE_ID <= 5, "Use a unique blade ID 1..5");
static_assert(HIT_COLLECTION_MS >= SWITCH_DEBOUNCE_MS, "Invalid hit timing");
