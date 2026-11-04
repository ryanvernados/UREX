# UREX Power Rune setup guide

This implementation connects five blade Type C boards to one main Type C over
CAN2, and connects an ESP32-S3 gateway to the main board over UART. The ESP32
uses Wi-Fi and MQTT to reach a local dashboard. Blade hit detection and motor
control work locally; internet access is not needed during operation.

The delivered code is a training-system implementation, not Shenzhen's original
firmware and not a certified competition referee system. Its CAN/UART protocols
are documented below. Physical wiring, impact timing, and pixel mapping require
verification on your assembled hardware.

## 1. Architecture and files

```text
 Phone / laptop browser
          | HTTP on your LAN (authenticated in hardware mode)
 Laptop / Pi: dashboard + MQTT broker + disk hit journal
          | MQTT over Wi-Fi
       ESP32-S3
          | UART, 115200 baud, 8N1, 3.3 V logic
     Main Type C
       |       |
       |       +-- CAN1, 1 Mbit/s --> C620 --> M3508 --> belt
       |
       +-- CAN2, 500 kbit/s --> slip ring --> five blade Type C boards
                                              |              |
                                          ten rings      five LED chains
```

| Location | What to change |
| --- | --- |
| `Core/Inc/rune_config.h` | Pulley teeth, gearbox ratio, speed ceiling, LED counts |
| `Core/Inc/motor_test.h` | C620 ID, PI gains, current limit, acceleration ramp |
| `Core/Src/rune_blade.c` | Blade pin arrays and physical LED pixel patterns |
| `esp32_gateway/include/gateway_secrets.h` | Your Wi-Fi and MQTT credentials |
| `dashboard/.env` | Broker/dashboard passwords, listen addresses and ports |
| `build.ps1 -BladeId N` | Board role: 0 main, 1..5 individual blade IDs |

The earlier `esp32_blade` example is superseded; do not use its pin map or flash
its image for this architecture.

## 2. Editable example values

The generic values requested for this starter are:

```c
// Core/Inc/rune_config.h
#define RUNE_GEARBOX_RATIO       19.0f
#define RUNE_MOTOR_PULLEY_TEETH   20.0f
#define RUNE_OUTPUT_PULLEY_TEETH  60.0f
#define RUNE_MAX_RPM             20.0f
#define BLADE_LED_COUNTS         {30, 30, 30, 30, 30}
```

LED order is arrow, upper border, lower border, central target, outer border.
The default maximum is 256 LEDs per chain. Increasing the maximum also grows
the DMA waveform buffer; the build must still fit the Type C's accessible RAM.
The five counts can differ. LEDs beyond each configured count will not be
updated. Start with a small strip so previous colors on unaddressed pixels do
not hide a mapping mistake.

```text
rotor RPM = rune RPM × gearbox ratio × rune pulley teeth / motor pulley teeth
```

At the example ratio, 10 rune RPM requests 570 rotor RPM. The gearbox value is
an approximation for this starter; use your actual reduction or calibrate it.
The main board derives displayed rune speed from the motor encoder and ratio:
it cannot detect belt slip without a separate rune-shaft encoder.

Small mode is constant 10 rune RPM. Manual mode uses the dashboard setting.
Large mode uses the illustrative profile
`(0.9*sin(1.94*t) + 1.19) rad/s`, converted to rune RPM and limited by
`RUNE_MAX_RPM`. Time is continuous in milliseconds. It is a repeatable training
profile; parameters are fixed and it is not the complete official randomization
or referee behavior. Direction is the commanded sign; confirm actual shaft
direction on the bench before trusting the CW/CCW labels.

## 3. Power and physical wiring

Power off before wiring. Use the Type C's proper board power input and a
separately sized 5 V supply for the WS2812B boards. Do not power a full blade's
LEDs from the Type C UART/CAN accessory outputs or the ESP32 regulator. Size
supplies, connectors, fuses, wire, and slip-ring current capacity for actual
loads. Keep a physical motor power disconnect available. Software Stop commands
zero current and allows coasting; it is not a mechanical brake.

All signal connections need a suitable ground reference. Never connect 24 V or
a 5 V power pin to a microcontroller GPIO. Feed WS2812B DIN through an appropriate
3.3 V to 5 V logic buffer, such as a 5 V powered 74AHCT125. Five chains require
five buffer channels. Add supply decoupling appropriate to the LED assembly.

### Main Type C to ESP32-S3

Use the **3-pin UART connector**, which is STM32 USART6. The shell's UART labels
can differ from STM32 peripheral names; identify the physical connector first.

| Type C 3-pin connector | STM32 IO | ESP32-S3 |
| --- | --- | --- |
| Pin 1 GND | Ground | GND |
| Pin 2 TXD | PG14 | GPIO18, UART RX |
| Pin 3 RXD | PG9 | GPIO17, UART TX |

Power the ESP32 from USB initially. Use 115200 baud, 8N1, with no flow control.
The S3 GPIOs are configurable in `esp32_gateway/include/gateway_config.h`.

### Motor bus

Keep the C620 on **main Type C CAN1**, at 1 Mbit/s. CAN1 connector pin 1 is CANL,
pin 2 is CANH. Set the C620 ID to 1 or change `MOTOR_ESC_ID`. The C620/motor need
their own proper supply; board USB power does not supply the motor.

### Blade CAN bus

Connect **CAN2 on the main and every blade Type C**, through the slip ring, at
500 kbit/s. Type C boards already contain CAN transceivers; an extra ESP32 CAN
transceiver is not required in this architecture.

| Type C CAN2 connector pin | Signal |
| --- | --- |
| 1 | 5 V accessory output: leave disconnected for CAN-only wiring |
| 2 | GND |
| 3 | CANH |
| 4 | CANL |

Route CANH/CANL as a pair, using a bus with short stubs. Account for any built-in
termination: the complete unpowered bus should have one 120-ohm termination at
each physical end (approximately 60 ohms across CANH/CANL). Do not blindly add a
resistor at all six boards. Verify the slip ring is suitable for this signaling.
The two connectors for a given CAN interface are the same bus, not extra buses.

### Each blade's ten switch inputs

These pins are repurposed as GPIO inputs on BLADE boards only; SPI/I2C/UART
operation on those connectors is disabled in the blade role.

| Ring | MCU pin | Accessible connector signal |
| --- | --- | --- |
| 1 | PB12 | Expansion SPI2_CS, expansion pin 1 |
| 2 | PB13 | Expansion SPI2_CLK, expansion pin 3 |
| 3 | PB14 | Expansion SPI2_MISO, expansion pin 7 |
| 4 | PB15 | Expansion SPI2_MOSI, expansion pin 5 |
| 5 | PF1 | Expansion I2C2_SCL, expansion pin 6 |
| 6 | PF0 | Expansion I2C2_SDA, expansion pin 8 |
| 7 | PG14 | 3-pin UART TXD, pin 2 |
| 8 | PG9 | 3-pin UART RXD, pin 3 |
| 9 | PA9 | 4-pin UART TXD, pin 2 |
| 10 | PB7 | 4-pin UART RXD, pin 1 |

Expansion pin 2 is GND and pin 4 is 3.3 V on an unmodified board. Confirm the
board has not been modified to output 5 V there. Match connector orientation
to DJI's diagrams rather than guessing from a cable's colors.

Each ring input closes to GND: switch COM to the ring signal, NO to ground,
NC unconnected. The switches within one ring are parallel. Internal pull-ups
are enabled; external pull-ups to **3.3 V** can improve the signal. Disconnect
any original F103 controller outputs/pull-ups that conflict with these inputs.

The sampler runs from SysTick every 1 ms with 2 ms debounce. It collects stable
ring signals for 15 ms after a trigger, choosing the highest ring. It requires
all switches released for 25 ms and a 100 ms cooldown. Validate real impact
pulses: sub-sample/debounce pulses can be missed, and a stuck switch prevents
rearming until released. These timings are in `rune_blade.c`.

### Each blade's five LED data outputs

| Chain | Type C PWM signal | MCU pin |
| --- | --- | --- |
| Arrow | TIM1_CH1 | PE9 |
| Upper border | TIM1_CH2 | PE11 |
| Lower border | TIM1_CH3 | PE13 |
| Central target | TIM1_CH4 | PE14 |
| Outer border | TIM8_CH1 | PC6 |

Use the named PWM signal pins as identified in DJI's connector diagram. The
firmware repurposes them as GPIO, driven by TIM1-update DMA to GPIO BSRR at
2.4 MHz (three phases per 800 kbit/s LED bit). DMA2 Stream5 Channel6 is reserved
for this driver. It updates one chain at a time while SysTick still samples
switches. Each transfer includes a greater than 400 microsecond low reset.
Verify timing and logic levels with a scope/logic analyzer before full assembly.

Patterns are generic: OFF, dim idle, solid target, and a central score bar after
a hit. They are not the original Shenzhen LED artwork. Adjust the per-pixel
mapping in `rune_blade.c` to match your PCB and desired target shape. The central
R-logo LED hardware is not implemented by this starter.

Pin references: [DJI Type C manual](https://rm-static.djicdn.com/tem/35228/RoboMaster%20Development%20Board%20Type%20C%20User%20Manual.pdf), printed pages 10–13 and appendix pages 17–18.

## 4. Build and flash the six Type C boards

From the UREX project root, using the ARM GCC toolchain bundled with STM32CubeIDE:

```powershell
./build.ps1 -BladeId 0
./build.ps1 -BladeId 1
./build.ps1 -BladeId 2
./build.ps1 -BladeId 3
./build.ps1 -BladeId 4
./build.ps1 -BladeId 5
```

The script searches PATH and `C:/ST`; pass `-ToolchainBin 'your/ARM/bin'` if needed.
Flash each correct role to its corresponding board using your ST-Link/CubeIDE
or STM32CubeProgrammer. All `.bin` images start at **0x08000000**:

| Board | Binary |
| --- | --- |
| Main | `build/urex_main/urex_main.bin` |
| Blade 1 | `build/urex_blade_1/urex_blade_1.bin` |
| Blades 2–5 | Corresponding `build/urex_blade_N/urex_blade_N.bin` |

Each blade ID must be unique. Label the boards. Building never flashes a board.
Older `build/motor_test.bin` or legacy Debug images may still exist: do not use
them for this system. In CubeIDE, the default project builds the MAIN role;
blade builds require `UREX_BLADE_ID=N` as a compiler define. The script avoids
accidentally building all five blade images with the same ID.

## 5. Start with the dashboard simulator

Install Node.js **22.9 or later**. In PowerShell:

```powershell
cd dashboard
npm.cmd ci
npm.cmd test
npm.cmd run demo
```

Open **http://127.0.0.1:8080**. Demo mode is clearly labeled and uses an isolated
loopback broker on port 1884. It never connects to real ESP32 hardware.

- Start rotation produces simulated feedback and one hit approximately every
  two seconds, stopping after five blades.
- For a manual hit, stop first, choose Blade 3, click Arm target, set a ring
  score, and click Send simulated hit.
- Confirm score, history, target state, and controller acknowledgment change.
- Export CSV contains all persisted demo sessions. New session resets the
  displayed session only; it does not delete the event journal.
- Close the browser and observe the simulator's control lease expire. Ctrl+C
  stops the local server.

The `dashboard/data/demo/` and `dashboard/data/live/` journals are separate.

## 6. Configure hardware MQTT mode

The dashboard includes a local MQTT broker; Mosquitto/Docker is not required.
The laptop/Pi running it must remain on and reachable from the ESP32's Wi-Fi.
Reserve that host's LAN IP if possible.

```powershell
cd dashboard
Copy-Item .env.example .env
# Edit .env in your editor, then:
npm.cmd start
```

Set **MQTT_PASSWORD** and **DASHBOARD_PASSWORD** to different strong values;
the server rejects unchanged CHANGE_ME placeholders. MQTT_USER defaults to
`urex-gateway`; DASHBOARD_USER defaults to `operator`.

Default HTTP binds only to 127.0.0.1:8080. To use a phone on your trusted LAN,
set `HTTP_HOST=0.0.0.0`, then open `http://YOUR_PC_LAN_IP:8080` and log in using
the dashboard credentials. Allow inbound TCP 1883 for the ESP32, and 8080 if
needed for your phone, restricted to your private network in the OS firewall.
Do not disable the firewall globally. Ports can be edited in `.env`.

This starter uses plain MQTT and HTTP with authentication on a **trusted private
LAN**. Credentials are not encrypted on the wire. Do not expose these ports to
the internet or use public Wi-Fi; use a secured network, VPN, or a properly
configured TLS reverse proxy/broker for a broader deployment. The default
device credentials cannot publish commands or subscribe to arbitrary topics.

## 7. Build the ESP32-S3 gateway

Copy `esp32_gateway/include/gateway_secrets.example.h` to
`esp32_gateway/include/gateway_secrets.h`. Set:

- Wi-Fi SSID/password for a 2.4 GHz network.
- MQTT host to the laptop/Pi LAN IP, not localhost.
- MQTT username/password to match `dashboard/.env`.

PlatformIO, with the `esp32_gateway` directory as the project:

```text
pio run
pio run -t upload
pio device monitor -b 115200
```

The target is ESP32-S3-DevKitC-1 N8 (8 MB flash; no PSRAM used). Adjust board/flash
settings if your S3 carrier differs. Dependencies are pinned in `platformio.ini`:
Arduino core 2.0.17, MQTT 2.5.2, ArduinoJson 6.21.5. Arduino IDE users can open
`esp32_gateway.ino`, keep the src/include folders beside it, install those
versions, choose an S3 board, and enable USB CDC On Boot. PlatformIO is the
verified build path. Select the correct native USB port for the Serial Monitor.

After uploading, the gateway should join Wi-Fi and connect to the broker. The
dashboard shows gateway/controller connectivity when valid UART telemetry arrives.
No credentials are committed: both local secret files are ignored by Git.

## 8. Bench commissioning in order

1. **No motor power:** flash the main and one blade. Confirm board IDs/roles.
2. Check grounds, voltage, and CAN termination. Connect CAN2 and UART.
3. Run the hardware dashboard and keep it open. Confirm the blade appears online.
4. Connect a short LED strip and a single switch. Use Arm target to test that
   blade without enabling the motor. Trigger the input, release it, and verify
   one hit, one ring score, and one journal row. Re-arm before another hit.
5. Check all ten rings and simultaneous rings. Check for stuck switches. Tune
   debounce/window timings with actual impacts as necessary.
6. Repeat for every blade. Connect the real LED chains only after setting their
   counts and checking supply/driver capacity and waveform timing.
7. Disconnect Wi-Fi/UART/CAN in turn. LEDs should go OFF after their command
   timeout. The controller must stop an active run when its lease or required
   hardware feedback is lost. Reconnection must not restart the motor.
8. Set real pulley and gearbox values. With the shaft unloaded and mechanism
   secured, power the C620. Check motor feedback appears. All five blades must
   be online for rotation unless you deliberately change
   `RUNE_REQUIRE_ALL_BLADES` for bench development.
9. Use low Manual RPM first. Check physical direction and measured shaft speed.
   Tune current limit, acceleration, and PI gains for the mechanism. Default
   current cap is raw 2000 (approximately 2.44 A), not guaranteed sufficient
   torque for a full-size rune.
10. Test Small and Large profiles, the five-hit completion stop, and the physical
    motor power disconnect before normal training.

## 9. Runtime behavior and delivery guarantees

- The main and blades boot stopped/off. Start requires fresh commands and a
  valid control lease. The main also requires motor feedback and, by default,
  all five online blades. Blade tests require only the selected online blade.
- Each run chooses a target from unhit online blades. A hit latches that blade,
  records its ring, and chooses another. Five accepted hits stop rotation. This
  is a training sequence, not every official referee timing rule.
- The dashboard requires a live page heartbeat. The backend supplies a lease
  only while that heartbeat and controller telemetry are fresh. The gateway
  forwards leases only while MQTT and the broker clock are fresh. The main's
  final lease timeout is 2 seconds; total shutdown delay after a lost browser or
  network can be several seconds because freshness intervals are layered.
  This is not a safety-rated emergency stop. Lost motor feedback is limited by
  the separate 100 ms motor feedback timeout.
- The blade command timeout is 1 second, and blade online status expires at
  500 ms on the main. If required blade communication is lost, the main stops.
  A loss/recovery requires a fresh Start or Arm command; it does not resume a run.
- Commands carry random IDs, the current controller boot ID, and a two-second
  expiry. They are never retained. The ESP32 rejects expired/stale-boot commands,
  and the main caches recent command results so retries do not rearm targets.
  The dashboard shows pending, acknowledged, rejected, or delivery timeout.
- UART uses newline frames with CRC16-CCITT, so malformed/noisy frames are
  discarded. The CRC is error detection, not cryptographic authentication.
- Main hit events have a random boot ID plus monotonically increasing event ID,
  run/session ID, blade, ring, and main-controller receipt time in milliseconds.
  Time is not a precise hardware impact timestamp or UTC. The server adds its
  own received-at UTC timestamp.
- The main buffers up to **31 unacknowledged events in RAM**, retries the head,
  and stops if full. The ESP32 forwards events with MQTT QoS 1. The backend
  validates and deduplicates by boot/event ID, appends and fsyncs the hit journal,
  and only then acknowledges it back through MQTT/UART. Repeated delivery does
  not increase the score twice. Buffered events survive Wi-Fi outages, but not
  main-controller power loss; the broker itself is in-memory. Hit records
  already on server disk survive server restart. This is not unlimited or
  power-loss-safe end-to-end storage.
- Session score is reconstructed from the journal. New session does not erase
  history. Preserve `dashboard/data/live/hits.jsonl` for your training records.

## 10. Protocol summary

### Blade CAN2

Standard DATA frames, DLC 8, 500 kbit/s, unique blade IDs 1..5.

| ID | Bytes |
| --- | --- |
| 0x300 + blade | version=1, sequence, mode, brightness, R, G, B, reserved=0 |
| 0x380 + blade | version=1, echoed sequence, state/flags, last ring, switch mask low/high, hit counter low/high |
| 0x3C0 + blade | version=1, boot nonce bytes 0..3 (little-endian), blade ID, firmware version=1, reserved=0 |

Modes are OFF=0, IDLE=1, TARGET=2, HIT=3. Status bit 0x10 means timed out/no
command. Main commands repeat unchanged every 100 ms per blade. Sequence changes
only with a new state/activation, so duplicate TARGET heartbeats cannot rearm a
hit blade. Status repeats every 50 ms and boot identification every 250 ms.
The main counts hits only from the selected target with its matching sequence.

### UART main to gateway

Each JSON payload is followed by `*HHHH\n`, with uppercase CRC16-CCITT-FALSE
(poly 0x1021, init 0xFFFF, no reflection/xorout) over the payload bytes.
Kinds are `telemetry`, `hit`, and `ack`.

Gateway to main uses the same CRC framing with ASCII payloads:

```text
L boot_id
C command_id boot_id action mode rpm_x10 direction blade
A boot_id event_id
```

Actions: Stop=0, Start=1, Settings=2, Arm blade=3, New session=4. Modes:
Manual=0, Small=1, Large=2. Direction: negative/CCW=0, positive/CW=1.

### MQTT topics

| Topic under `urex/rune/` | Purpose |
| --- | --- |
| telemetry | Main controller state, speed, blades and current session |
| hit | Retried hit event, QoS 1, non-retained |
| event_ack | Server journal acknowledgment, QoS 1, non-retained |
| cmd | Expiring operator command, non-retained |
| ack | Controller command result, QoS 1, non-retained |
| gateway | Gateway online/offline, retained status and Last Will |
| clock | Local server clock for command expiry, no internet NTP dependency |
| lease | Fresh operator-control lease, non-retained |

The web browser uses the local HTTP/SSE API; it does not receive MQTT credentials.
You can use MQTT tools with a deliberately configured broker role for debugging;
the default device role cannot subscribe to all telemetry topics or issue commands.

## 11. Troubleshooting

| Symptom | Check |
| --- | --- |
| Dashboard cannot start | Node version, npm ci, changed .env passwords, occupied ports |
| Gateway offline | 2.4 GHz Wi-Fi, broker LAN IP/credentials, private firewall rule, actual hardware mode rather than demo |
| Gateway online but controller offline | Crossed TX/RX, shared GND, 115200 baud, main-role image, 3-pin UART orientation |
| A blade is offline | Unique image ID, CAN2 rather than CAN1, 500 kbit/s, termination, ground, slip-ring contacts |
| Start rejected | Read the command result; check lease, motor feedback, all blades, event delivery, existing blade test |
| Motor runs at wrong speed | Actual pulley teeth/gearbox ratio and belt slip; dashboard displays derived rune RPM |
| Ring stuck or not detected | GPIO mapping, NO/COM wiring, 3.3 V pull-up, mechanical preload, pulse/debounce timing |
| LEDs wrong or flicker | Chain counts/order, DIN/DOUT, logic buffer, supply sag, scope the reset/data timing |
| Hits buffered | MQTT/server availability, writable journal disk, event_ack subscription and UART RX path |
| Applied command but stale display | Controller acknowledgment applies settings; verify fresh telemetry, not only a button response |

## 12. Verification and remaining hardware work

Automated checks cover journal deduplication/restart, incomplete-write recovery,
invalid hit rejection, command validation/expiry, MQTT simulator hit delivery,
control-lease expiry, authentication, topic permissions, non-retained commands,
and controller acknowledgment. Firmware build checks cover both Type C roles
and the ESP32-S3 gateway. See the delivery message for final build results.

No physical boards have been flashed or tested by this session. Motor behavior,
impact detection, LED waveforms/pixel mapping, and slip-ring reliability must be
bench-verified. Browser preview was unavailable in this environment; dashboard
API behavior was exercised by automated integration tests.

Further references: [ESP32 MQTT](https://github.com/256dpi/arduino-mqtt),
[MQTT.js](https://github.com/mqttjs/MQTT.js),
[Aedes broker](https://github.com/moscajs/aedes),
[RoboMaster 2026 rotation rules](https://bbs-web-static.robomaster.com/1f59fe9c9d154752a5e456e1f7139d4c1777519819115/RoboMaster%202026%20University%20Championship%20Rule%20Manual%20V1.4.2%EF%BC%8820260430%EF%BC%89.pdf).
