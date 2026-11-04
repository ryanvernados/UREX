#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <driver/twai.h>
#include "../include/blade_config.h"
#include "../include/blade_protocol.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "This pin map is for ESP32-S3. Select an S3 board."
#endif

using namespace BladeProtocol;
struct HitEvent { uint32_t at; uint16_t mask; uint8_t ring; };
QueueHandle_t hitQueue;
portMUX_TYPE inputMux = portMUX_INITIALIZER_UNLOCKED;
uint16_t sharedMask = 0;
bool eventOverflow = false;
Adafruit_NeoPixel strips[5] = {
    Adafruit_NeoPixel(LED_COUNTS[0], LED_PINS[0], NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(LED_COUNTS[1], LED_PINS[1], NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(LED_COUNTS[2], LED_PINS[2], NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(LED_COUNTS[3], LED_PINS[3], NEO_GRB + NEO_KHZ800),
    Adafruit_NeoPixel(LED_COUNTS[4], LED_PINS[4], NEO_GRB + NEO_KHZ800)
};
Mode mode = OFF;
uint8_t lastCommand[8] = {};
uint8_t sequence = 0, lastRing = 0;
uint8_t brightness = 32, red = 255, green = 0, blue = 0;
uint16_t hitCount = 0;
uint32_t lastCommandAt = 0, armedAt = 0;
bool haveCommand = false, timedOut = true, bench = false;
bool ledDirty = true, statusDirty = true;

void fatal(const char *message) {
    Serial.println(message);
    for (auto &strip : strips) { strip.clear(); strip.show(); }
    while (true) { delay(1000); }
}

// Higher-priority sampler keeps running while the loop sends LED frames.
// Pulses shorter than the sample/debounce interval can be missed: tune on hardware.
void sampleInputs(void *) {
    uint16_t previousRaw = 0, stable = 0, collected = 0;
    uint32_t changedAt[10] = {};
    uint32_t releasedAt = millis(), collectAt = 0, lastHitAt = 0;
    bool ready = false, collecting = false, released = false;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        uint32_t now = millis();
        uint16_t raw = 0;
        for (unsigned i = 0; i < 10; ++i) {
            uint16_t bit = uint16_t(1U << i);
            if (digitalRead(RING_PINS[i]) == LOW) { raw |= bit; }
            if ((raw ^ previousRaw) & bit) { changedAt[i] = now; }
            if (now - changedAt[i] >= SWITCH_DEBOUNCE_MS) {
                stable = (stable & ~bit) | (raw & bit);
            }
        }
        previousRaw = raw;
        portENTER_CRITICAL(&inputMux);
        sharedMask = stable;
        portEXIT_CRITICAL(&inputMux);
        if (raw == 0 && stable == 0) {
            if (!released) { releasedAt = now; released = true; }
            if (!collecting && now - releasedAt >= RELEASE_STABLE_MS &&
                now - lastHitAt >= HIT_COOLDOWN_MS) { ready = true; }
        } else { released = false; }
        if (ready && stable) {
            ready = false; collecting = true; collectAt = now; collected = stable;
        }
        if (collecting) {
            collected |= stable;
            if (now - collectAt >= HIT_COLLECTION_MS) {
                uint8_t ring = 0;
                for (unsigned i = 0; i < 10; ++i) {
                    if (collected & (1U << i)) { ring = i + 1; }
                }
                HitEvent event = {collectAt, collected, ring};
                if (xQueueSend(hitQueue, &event, 0) != pdTRUE) {
                    portENTER_CRITICAL(&inputMux);
                    eventOverflow = true;
                    portEXIT_CRITICAL(&inputMux);
                }
                collecting = false; lastHitAt = now; released = false;
            }
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1));
    }
}

void applyMode(Mode requested, uint32_t now) {
    mode = requested;
    if (mode == TARGET) { armedAt = now; lastRing = 0; }
    if (mode == OFF || mode == IDLE) { lastRing = 0; }
    ledDirty = statusDirty = true;
}

void receiveCommands(uint32_t now) {
    twai_message_t message;
    for (unsigned n = 0; n < 32 && twai_receive(&message, 0) == ESP_OK; ++n) {
        if (bench || message.extd || message.rtr || message.data_length_code != 8 ||
            message.identifier != COMMAND_BASE + BLADE_ID) { continue; }
        const uint8_t *d = message.data;
        if (d[0] != VERSION || d[2] > HIT || d[7] != 0) { continue; }
        bool duplicate = haveCommand && d[1] == sequence;
        // Repeated sequence must be an identical heartbeat, not a changed command.
        if (duplicate && memcmp(d, lastCommand, 8) != 0) { continue; }
        lastCommandAt = now;
        if (duplicate) { continue; } // Do NOT rearm a locally hit target.
        memcpy(lastCommand, d, 8);
        haveCommand = true; timedOut = false; sequence = d[1];
        brightness = min(d[3], MAX_BRIGHTNESS);
        red = d[4]; green = d[5]; blue = d[6];
        applyMode(static_cast<Mode>(d[2]), now);
    }
}

void serviceCan(uint32_t now) {
    twai_status_info_t info;
    if (twai_get_status_info(&info) != ESP_OK) { return; }
    if (info.state == TWAI_STATE_BUS_OFF) {
        if (!bench) {
            haveCommand = false; timedOut = true;
            applyMode(OFF, now);
        }
        twai_initiate_recovery();
    } else if (info.state == TWAI_STATE_STOPPED) {
        // Recovery is complete. Require a fresh frame after restarting.
        twai_clear_receive_queue();
        twai_start();
    }
}

void sendStatus() {
    if (bench) { return; } // Bench tests also work with no CAN peer to ACK.
    uint16_t mask;
    bool overflow;
    portENTER_CRITICAL(&inputMux);
    mask = sharedMask; overflow = eventOverflow;
    portEXIT_CRITICAL(&inputMux);
    twai_message_t message = {};
    message.ss = 1; // Single shot: do not retry obsolete telemetry indefinitely.
    message.identifier = STATUS_BASE + BLADE_ID;
    message.data_length_code = 8;
    message.data[0] = VERSION;
    message.data[1] = sequence;
    message.data[2] = uint8_t(mode) | (timedOut ? FLAG_TIMEOUT : 0) |
                      (bench ? FLAG_BENCH : 0) | (overflow ? FLAG_EVENT_OVERFLOW : 0);
    message.data[3] = lastRing;
    message.data[4] = mask & 0xff;
    message.data[5] = mask >> 8;
    message.data[6] = hitCount & 0xff;
    message.data[7] = hitCount >> 8;
    // Nonblocking, no backlog of old status messages when disconnected.
    twai_transmit(&message, 0);
}

void renderLeds() {
    // Generic patterns: NOT a replica of the original PCB's pixel artwork.
    for (unsigned section = 0; section < 5; ++section) {
        auto &strip = strips[section];
        strip.setBrightness(brightness);
        strip.clear();
        if (mode != OFF) {
            uint8_t divisor = mode == IDLE ? 16 : 1;
            uint32_t color = strip.Color(red / divisor, green / divisor, blue / divisor);
            uint16_t count = strip.numPixels();
            // Simple score bar on central target after a hit. Adjust pixel map later.
            if (mode == HIT && section == 3) {
                count = (uint32_t(lastRing) * strip.numPixels() + 9) / 10;
            }
            strip.fill(color, 0, count);
            // NeoPixel fill(..., count=0) means ALL pixels, so explicitly clear score 0.
            if (mode == HIT && section == 3 && lastRing == 0) { strip.clear(); }
        }
        strip.show();
    }
    ledDirty = false;
}

void serialBench(uint32_t now) {
    for (unsigned n = 0; n < 16 && Serial.available(); ++n) {
        int c = Serial.read();
        if (c == '\r' || c == '\n') { continue; }
        if (c == 'c') {
            bench = false; haveCommand = false; timedOut = true;
            applyMode(OFF, now);
            Serial.println("CAN mode; waiting for command.");
        } else if (c == 't' || c == 'i' || c == 'o') {
            bench = true; haveCommand = false; timedOut = false;
            lastCommandAt = now;
            brightness = 32; red = 255; green = 0; blue = 0;
            applyMode(c == 't' ? TARGET : (c == 'i' ? IDLE : OFF), now);
            Serial.println("Local LED test for 10s; CAN commands ignored. c returns to CAN.");
        }
    }
}

void setup() {
    Serial.begin(115200);
    for (auto &strip : strips) {
        strip.begin();
        if (!strip.getPixels()) { fatal("LED buffer allocation failed"); }
        strip.clear(); strip.show();
    }
    for (auto pin : RING_PINS) { pinMode(pin, INPUT_PULLUP); }
    hitQueue = xQueueCreate(8, sizeof(HitEvent));
    if (!hitQueue) { fatal("Hit queue allocation failed"); }
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(CAN_TX_PIN), static_cast<gpio_num_t>(CAN_RX_PIN), TWAI_MODE_NORMAL);
    general.rx_queue_len = 32;
    general.tx_queue_len = 0;
    twai_timing_config_t timing = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    if (twai_driver_install(&general, &timing, &filter) != ESP_OK || twai_start() != ESP_OK) {
        fatal("TWAI initialization failed");
    }
    if (xTaskCreatePinnedToCore(sampleInputs, "rings", 3072, nullptr, 3, nullptr, 1) != pdPASS) {
        fatal("Input task creation failed");
    }
    Serial.printf("UREX S3 blade %u; CAN 500k; t=target, i=idle, o=off, c=CAN\n", BLADE_ID);
}

void loop() {
    uint32_t now = millis();
    serialBench(now);
    serviceCan(now);
    receiveCommands(now);
    uint32_t timeout = bench ? SERIAL_BENCH_TIMEOUT_MS : COMMAND_TIMEOUT_MS;
    if (!timedOut && now - lastCommandAt >= timeout) {
        bench = false; haveCommand = false; timedOut = true;
        applyMode(OFF, now);
    }
    HitEvent event;
    while (xQueueReceive(hitQueue, &event, 0) == pdTRUE) {
        if (mode == TARGET && !timedOut && int32_t(event.at - armedAt) >= 0) {
            lastRing = event.ring;
            ++hitCount;
            applyMode(HIT, now);
            Serial.printf("HIT ring=%u mask=0x%03x count=%u\n", lastRing, event.mask, hitCount);
        }
    }
    static uint32_t statusAt = 0, frameAt = 0;
    if (statusDirty || now - statusAt >= STATUS_PERIOD_MS) {
        sendStatus(); statusAt = now; statusDirty = false;
    }
    if (ledDirty && now - frameAt >= LED_FRAME_MS) { renderLeds(); frameAt = now; }
    delay(1);
}
