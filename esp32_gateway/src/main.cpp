#include <Arduino.h>
#include <WiFi.h>
#include <MQTT.h>
#include <ArduinoJson.h>
#include "../include/gateway_config.h"

WiFiClient socketClient;
MQTTClient mqtt(2048);
uint32_t controllerBoot, clockAt, leaseAt;
uint64_t serverClock;
bool haveClock, haveLease;
String eventPayload, telemetryPayload, ackPayload;
uint32_t eventBoot, eventId;
uint32_t controllerAt;
String pendingCommand;
uint32_t pendingId, pendingBoot;
uint64_t pendingExpiry;
char line[1600];
unsigned lineUsed;
bool dropping;

uint16_t crc16(const String &text) {
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < text.length(); ++i) {
        crc ^= uint8_t(text[i]) << 8;
        for (unsigned n = 0; n < 8; ++n) { crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1; }
    }
    return crc;
}
void serialSend(const String &text) {
    char suffix[8]; snprintf(suffix, sizeof(suffix), "*%04X\n", crc16(text));
    Serial2.print(text); Serial2.print(suffix);
}
uint64_t estimatedTime() { return serverClock + uint32_t(millis() - clockAt); }
bool clockValid() { return haveClock && millis() - clockAt < 3000; }

void messageReceived(String &topic, String &payload) {
    StaticJsonDocument<1536> doc;
    if (deserializeJson(doc, payload)) { return; }
    if (topic == "urex/rune/clock") {
        if (!doc["time_ms"].is<uint64_t>()) { return; }
        serverClock = doc["time_ms"].as<uint64_t>(); clockAt = millis(); haveClock = true;
    } else if (topic == "urex/rune/lease") {
        uint64_t issued = doc["time_ms"] | uint64_t(0);
        if (clockValid() && issued <= estimatedTime() + 1000 && estimatedTime() <= issued + 1500) {
            haveLease = true; leaseAt = millis();
        }
    } else if (topic == "urex/rune/event_ack") {
        uint32_t boot = doc["boot_id"] | 0U, id = doc["event_id"] | 0U;
        if (boot == eventBoot && id == eventId && eventPayload.length()) {
            serialSend("A " + String(boot) + " " + String(id));
            eventPayload = "";
        }
    } else if (topic == "urex/rune/cmd") {
        uint64_t expiry = doc["expires_at"] | uint64_t(0);
        if (!clockValid() || expiry <= estimatedTime() || expiry - estimatedTime() > 2500) { return; }
        uint32_t id = doc["id"] | 0U, boot = doc["boot_id"] | 0U;
        unsigned action = doc["action"] | 99U, mode = doc["mode"] | 99U;
        unsigned rpm = doc["rpm_x10"] | 999U, dir = doc["direction"] | 99U, blade = doc["blade"] | 99U;
        if (!id || !boot || boot != controllerBoot || action > 4 || mode > 2 || rpm > 200 || dir > 1 || blade > 5) { return; }
        pendingId = id; pendingBoot = boot; pendingExpiry = expiry;
        pendingCommand = "C " + String(id) + " " + String(boot) + " " + String(action) + " " +
                         String(mode) + " " + String(rpm) + " " + String(dir) + " " + String(blade);
    }
}
void readSerial() {
    for (unsigned n = 0; n < 2048 && Serial2.available(); ++n) {
        char c = Serial2.read();
        if (c == '\n') {
            if (dropping) { dropping = false; lineUsed = 0; continue; }
            line[lineUsed] = 0; lineUsed = 0;
            char *star = strrchr(line, '*');
            if (!star || strlen(star + 1) != 4) { continue; }
            char *end;
            unsigned long expected = strtoul(star + 1, &end, 16);
            if (*end) { continue; }
            *star = 0;
            String text(line);
            if (crc16(text) != expected) { continue; }
            StaticJsonDocument<1536> doc;
            if (deserializeJson(doc, text)) { continue; }
            const char *kind = doc["kind"] | "";
            if (!strcmp(kind, "telemetry")) {
                uint32_t boot = doc["boot_id"] | 0U;
                if (controllerBoot && controllerBoot != boot) { pendingCommand = ""; }
                controllerBoot = boot; controllerAt = millis(); telemetryPayload = text;
            } else if (!strcmp(kind, "hit")) {
                eventPayload = text;
                eventBoot = doc["boot_id"] | 0U; eventId = doc["event_id"] | 0U;
            } else if (!strcmp(kind, "ack")) {
                if ((doc["boot_id"] | 0U) == pendingBoot && (doc["id"] | 0U) == pendingId) { pendingCommand = ""; }
                ackPayload = text;
            }
        } else if (c != '\r' && !dropping) {
            if (lineUsed + 1 < sizeof(line)) { line[lineUsed++] = c; }
            else { dropping = true; lineUsed = 0; }
        }
    }
}
void setup() {
    Serial.begin(115200);
    Serial2.setRxBufferSize(4096);
    Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    WiFi.begin(UREX_WIFI_SSID, UREX_WIFI_PASSWORD);
    mqtt.begin(UREX_MQTT_HOST, MQTT_PORT, socketClient);
    mqtt.onMessage(messageReceived);
    mqtt.setOptions(10, true, 500); // Clean session; bounded network calls.
    mqtt.setWill("urex/rune/gateway", "{\"online\":false}", true, 1);
    Serial.println("UREX MQTT gateway; UART RX18 TX17, 115200. Waiting for Wi-Fi.");
}
void loop() {
    uint32_t now = millis();
    readSerial();
    static uint32_t reconnectAt, leaseSendAt, commandSendAt, eventSendAt, telemetryAt;
    if (WiFi.status() == WL_CONNECTED && !mqtt.connected() && now - reconnectAt > 3000) {
        reconnectAt = now;
        haveClock = haveLease = false; pendingCommand = "";
        String clientId = "urex-gateway-" + String(uint32_t(ESP.getEfuseMac()), HEX);
        if (mqtt.connect(clientId.c_str(), UREX_MQTT_USER, UREX_MQTT_PASSWORD)) {
            mqtt.subscribe("urex/rune/cmd", 1); mqtt.subscribe("urex/rune/clock", 0);
            mqtt.subscribe("urex/rune/lease", 0); mqtt.subscribe("urex/rune/event_ack", 1);
            mqtt.publish("urex/rune/gateway", "{\"online\":true}", true, 1);
        }
    }
    if (mqtt.connected()) {
        mqtt.loop();
        if (clockValid() && haveLease && millis() - leaseAt < 1500 && controllerBoot && now - leaseSendAt >= 250) {
            serialSend("L " + String(controllerBoot)); leaseSendAt = now;
        }
        if (pendingCommand.length()) {
            if (!clockValid() || pendingExpiry <= estimatedTime()) { pendingCommand = ""; }
            else if (now - commandSendAt >= 150) { serialSend(pendingCommand); commandSendAt = now; }
        }
        if (telemetryPayload.length() && now - controllerAt < 1000 && now - telemetryAt >= 250) {
            mqtt.publish("urex/rune/telemetry", telemetryPayload, false, 0); telemetryAt = now;
        }
        if (ackPayload.length() && mqtt.publish("urex/rune/ack", ackPayload, false, 1)) { ackPayload = ""; }
        if (eventPayload.length() && now - eventSendAt >= 500) {
            String copy = eventPayload; // Event ACK callbacks may clear the original during publish.
            mqtt.publish("urex/rune/hit", copy, false, 1); eventSendAt = now;
        }
    } else { haveLease = false; pendingCommand = ""; }
    delay(1);
}
