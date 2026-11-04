# UREX ESP32-S3 blade controller

This starter replaces one F103 blade controller with one ESP32-S3. It reads ten
active-low ring inputs, drives five WS2812B chains, and exchanges commands and
status over CAN. Use one board per blade, with a different BLADE_ID (1..5).

This is new firmware with a new UREX protocol, not a binary-compatible port of
Shenzhen University's F103 firmware. It does not control the motor or provide
Wi-Fi controls. The existing UREX motor firmware is unchanged. The optional
Type C CAN2 example below supplies matching commands for a first bench test.

## Hardware and pins

Default build: ESP32-S3-DevKitC-1 N8, 8 MB flash, no PSRAM required. Other S3
carrier boards can use different exposed pins or flash settings: check yours
before connecting it. These are GPIO numbers, not header positions. Configuration
is in `include/blade_config.h`.

| Function | ESP32-S3 GPIO |
| --- | --- |
| CAN TX to transceiver TXD/D | 1 |
| CAN RX from transceiver RXD/R | 2 |
| Ring 1 through ring 10 | 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 |
| Arrow LED data | 14 |
| Upper border LED data | 15 |
| Lower border LED data | 16 |
| Central target LED data | 17 |
| Outer border LED data | 18 |

The S3 has a CAN controller but needs an external CAN transceiver with 3.3 V
compatible logic, for example an appropriate SN65HVD230 module. Connect its
CANH/CANL to Type C CAN2 and connect signal ground. GPIO1/2 must not connect
directly to CANH/CANL. Put the transceiver into normal operation according to
its board documentation. The blade bus runs at **500 kbit/s**, independent of
the C620 CAN1 bus at 1 Mbit/s. Fit one 120-ohm termination at each physical bus
end, not on every blade. Use a bus with short stubs through the slip ring.

For each ring, connect the PCB's switch signal/COM to its GPIO and NO to GND,
leaving NC unconnected. Each ring's switches are connected in parallel. GPIO
pull-ups are enabled; use an external approximately 10k pull-up to 3.3 V on each
input for a more predictable bench circuit. Verify your PCB has no conflicting
pull-up to 5 V. Disconnect the old F103 output drivers before attaching the ESP32.

Drive LED DIN through a suitable 3.3 V to 5 V logic buffer, such as a 5 V powered
74AHCT125 with its output enabled, sharing ground. Five independent chains need
five buffer channels. Connect each output to that chain's FIRST DIN, not DOUT.
Power LEDs separately from a properly sized 5 V supply. Do not feed the large
LED boards from USB or the ESP32's regulator. Select supply, wiring, fusing, and
slip-ring current ratings for the measured load. Start at low brightness.

**LED_COUNTS defaults to 30 per chain solely for initial testing.** Count your
actual LEDs and update it. The code uses generic solid patterns and a score bar;
it does not reproduce Shenzhen's original pixel layout or official rune artwork.
Adapt `renderLeds()` after confirming physical pixel order. The GPIO assignment
also does not match the F103 PCB footprint: this requires rewiring/an adapter.

## Build and upload

Open this folder as a PlatformIO project in VS Code:

```text
pio run
pio run -t upload
pio device monitor -b 115200
```

Pinned dependencies: PlatformIO Espressif32 6.10.0, its Arduino ESP32 2.0.17
framework, and Adafruit NeoPixel 1.12.3. USB CDC is enabled on the S3 native USB
port. If your board has separate USB and USB-UART ports, select the appropriate
upload/monitor port; UART0 output is not the selected Serial interface here.
If necessary, use the board's BOOT/RESET procedure to enter download mode.

Arduino IDE alternative: install Espressif's ESP32 board package **2.0.17** and
Adafruit NeoPixel **1.12.3**. Open `esp32_blade.ino` in this folder, keeping the
`src` and `include` folders beside it. Use an ESP32-S3 board definition and enable
USB CDC on boot. Arduino automatically compiles `src/main.cpp`. Keep these pinned
versions for this example: the LED driver's RMT resource management differs in
Arduino core 3.x, particularly with five outputs. The PlatformIO configuration
is the verified build target; other core/library versions need separate work.

## First test without the motor

Keep the C620 unpowered while testing blade electronics. The current UREX motor
firmware starts the motor automatically; do not assume that connecting an ESP32
changes that behavior.

1. Connect a small WS2812B strip and one switch before connecting a full blade.
2. Upload this firmware. LEDs should be OFF at boot.
3. Open the native USB Serial Monitor and send `t` to arm a local target.
4. Briefly connect ring 10 input (GPIO13) to GND. The monitor should report
   `HIT ring=10`; the center chain displays a full score bar.
5. Release all switches, wait at least 100 ms, and send `t` again to rearm.
6. Try other ring inputs. With simultaneous rings, the highest ring wins.
7. Send `i` for idle, `o` for off, or `c` to return to CAN mode.

Local tests ignore CAN commands and automatically expire to OFF after 10 seconds.
They work without a connected CAN peer. This code deliberately keeps Wi-Fi off.
Each new `t` is a new target activation; switch activity only counts in TARGET.

Input sampling runs in a separate 1 ms task, with a 2 ms debounce. After a
trigger it collects stable inputs for 15 ms and chooses the highest ring; it
requires all switches released for 25 ms plus a 100 ms cooldown before another
physical hit. These values are starting points, not validated ball-impact
timings. Verify with real impact pulses; very short pulses can be missed. A
switch held at startup must be released before it can produce a hit.

## CAN protocol v1

Classic CAN, standard 11-bit DATA frames, DLC 8, 500 kbit/s. No broadcast commands.
Command ID = **0x300 + blade ID**; status ID = **0x380 + blade ID**.

| Byte | Command | Status |
| --- | --- | --- |
| 0 | Version = 1 | Version = 1 |
| 1 | Sequence, 0..255 | Last accepted sequence |
| 2 | Mode | Mode in bits 0..1; flags below |
| 3 | Brightness, 0..255 | Last detected ring, 0..10 |
| 4 | Red, 0..255 | Current debounced switch mask low byte |
| 5 | Green, 0..255 | Current debounced switch mask high byte |
| 6 | Blue, 0..255 | Hit counter low byte |
| 7 | Reserved = 0 | Hit counter high byte |

Modes: 0 OFF, 1 IDLE (dim), 2 TARGET (armed), 3 HIT (activated).
Brightness is capped at MAX_BRIGHTNESS (64 by default). Status flags are 0x10
command timeout/no command, 0x20 local bench mode, 0x40 input-event queue overflow
(latched until reboot). Bench status is not sent over CAN; the flag is reserved
for future telemetry. Switch mask bit 0 is ring 1; bit 9 is ring 10. Multibyte
status fields are little-endian. The hit counter wraps at 65535 and resets on boot.

Example for blade 1: ID **0x301**, bytes **01 01 02 20 FF 00 00 00**:
version 1, sequence 1, TARGET, brightness 32, red. It replies at **0x381**.

Send that IDENTICAL frame every 100 ms as a heartbeat. Increment sequence modulo
256 whenever you change a setting or rearm a target. Do not advance sequence on
every heartbeat: that would continuously rearm the blade. A repeated sequence
with changed contents is rejected. A local hit changes mode to HIT, increments
the counter, and stays latched despite TARGET heartbeats with the same sequence.
Send a fresh sequence to rearm. One active target counts at most one hit.

Status is sent every 50 ms and on a state change, with no queued backlog. It is
not an acknowledged event log: the main controller must compare hit counters and
read the ring before issuing the next command. The firmware does not persist
events across a reboot and CAN does not authenticate senders.

After 1 second without a valid command, the blade goes OFF. Bus-off also sets it
OFF and starts recovery. After timeout/recovery, the next valid command may
reactivate the target, even with the prior sequence. No full game sequencing,
scoring history, or automatic next-target selection is included.

## Optional UREX Type C sender

`controller_example/urex_blade_can.c` and `.h` are an OPTIONAL HAL integration
example, not part of the active firmware build. They initialize CAN2 on PB5/PB6,
with a separate receive filter bank, and heartbeat five independent commands.
They assume the existing UREX 42 MHz APB1 clock and the current CAN1 setup.

For a bench test:

1. Leave the C620 unpowered. If motor power will be present, first change the
   existing `motor_enabled` initializer to 0 in `Core/Src/motor_test.c`.
2. Copy the helper `.c` into `Core/Src` and its `.h` into `Core/Inc`.
3. Include `urex_blade_can.h` from `main.c`.
4. After `MotorTest_Init()`, add:

```c
if (UrexBladeCAN_Init() != HAL_OK) { Error_Handler(); }
UrexBladeCAN_Command(1, UREX_BLADE_TARGET, 32, 255, 0, 0);
```

5. Change the loop to:

```c
while (1) {
    MotorTest_Update();
    UrexBladeCAN_Update();
}
```

6. On the ESP32, send `c` to exit any serial test. Verify red TARGET LEDs,
   `urex_blades[0].online`, and the hit counter/ring in the STM32 debugger.
7. Call `UrexBladeCAN_Command()` once per new target or setting change. It is
   nonblocking and queues the desired command for the next heartbeat slot.
8. Disconnect/stop Type C CAN2 commands and verify the blade goes OFF within
   approximately 1 second plus the LED transmission time. Restore commands and
   verify recovery. Check every blade with a unique ID before rotating anything.

The sender does not implement a game controller or ESP32 Wi-Fi bridge. It sends
OFF to the other four blades. If integrating another firmware, merge its CAN2
initialization/filter ownership instead of initializing the same peripheral twice.

## References and verification limits

- [Espressif ESP32-S3 TWAI driver](https://docs.espressif.com/projects/esp-idf/en/v4.4.4/esp32s3/api-reference/peripherals/twai.html)
- [Adafruit NeoPixel source](https://github.com/adafruit/Adafruit_NeoPixel)
- [PlatformIO S3 DevKitC target](https://docs.platformio.org/en/latest/boards/espressif32/esp32-s3-devkitc-1.html)

Compilation cannot verify wiring, pulse timing, LED mapping, slip-ring signal
quality, or electrical load. Perform the bench checks before mechanical testing.
