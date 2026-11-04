#pragma once
#if __has_include("gateway_secrets.h")
#include "gateway_secrets.h"
#else
// Copy gateway_secrets.example.h to gateway_secrets.h and fill in your LAN values.
#define UREX_WIFI_SSID "CHANGE_ME"
#define UREX_WIFI_PASSWORD "CHANGE_ME"
#define UREX_MQTT_HOST "192.168.1.10"
#define UREX_MQTT_USER "urex-gateway"
#define UREX_MQTT_PASSWORD "CHANGE_ME_gateway_password"
#endif
constexpr int UART_TX_PIN = 17; // To Type C USART6 RX: PG9, 3-pin port pin 3.
constexpr int UART_RX_PIN = 18; // From Type C USART6 TX: PG14, 3-pin port pin 2.
constexpr uint16_t MQTT_PORT = 1883;
