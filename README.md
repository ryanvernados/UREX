# UREX Power Rune

Local MQTT dashboard, ESP32-S3 Wi-Fi gateway, one main RoboMaster Type C board,
and one Type C board per blade. Hit detection stays on the blade boards; motor
control and target sequencing stay on the main board. MQTT carries telemetry,
acknowledged hit events, and expiring operator commands over your private LAN.

**Start with [SETUP_GUIDE.md](SETUP_GUIDE.md).** It includes wiring, configuration,
building/flashing, demo operation, bench checks, and troubleshooting.

- `dashboard/`: local authenticated web dashboard, MQTT broker, simulator,
  durable hit journal, CSV export, and automated tests.
- `esp32_gateway/`: ESP32-S3 UART-to-MQTT gateway, no motor control loop.
- `Core/`: Type C main and blade roles, selected by `UREX_BLADE_ID`.
- `Core/Inc/rune_config.h`: example pulley ratio, limits, LED counts.
- `esp32_blade/`: superseded ESP32-per-blade experiment. Not used by this design.

The motor boots **disabled**. A fresh Start command, valid control lease, motor
feedback, and all five online blades are required to rotate it. A stationary
single-blade test is available without motor feedback. Stop commands send zero
current; they do not actively brake the mechanism. Provide a physical stop.

Example values are 19:1 gearbox, 20T motor pulley, 60T output pulley, and 30 LEDs
per chain. Update these before a powered mechanism test. The lighting is generic
and needs your PCB's actual pixel mapping to reproduce official rune artwork.

Quick dashboard demo, from PowerShell with Node.js 22.9+ installed:

```powershell
cd dashboard
npm.cmd ci
npm.cmd run demo
```

Open http://127.0.0.1:8080. Demo mode uses a separate loopback-only MQTT broker
on port 1884 and does not connect to physical hardware. Click Start for simulated
motion/hits, or select a blade, Arm target, then Send simulated hit.

Build Type C firmware from this project root:

```powershell
./build.ps1 -BladeId 0  # Main board
./build.ps1 -BladeId 1  # Blade 1; repeat for 2..5
```

Firmware is built under `build/urex_main/` or `build/urex_blade_N/`. Building does
not flash anything. Keep the legacy `motor_test.bin` out of the flashing flow:
older builds may contain the previous auto-start behavior.
