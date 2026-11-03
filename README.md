# M3508 / C620 CAN1 motor test

Minimal bare-metal firmware for RoboMaster Type C (STM32F407IGH6, 12 MHz HSE).
Only CAN1 and the HAL millisecond SysTick are initialized. No remote receiver,
FreeRTOS, IMU, chassis, gimbal, launcher, referee system, USB or CAN2 is required.

**The motor starts automatically after a two-second delay and valid feedback.**
Secure the motor for a bench test with its shaft free before powering the C620.

1. Set the C620 ID to **1**, or change `MOTOR_ESC_ID` in `Core/Inc/motor_test.h`.
2. Connect the powered C620 to the board's CAN1, with CAN-H to CAN-H and CAN-L
   to CAN-L. Verify the bus has termination at its two ends. USB power for the
   board does not supply motor power to the C620.
3. Import this directory as an existing STM32CubeIDE project. The project name
   is `UREX`; use the `UREX Debug` launch configuration.
   Refresh the project and Clean / Build, then flash and run it with your debugger.
4. Watch `motor_status` in Live Expressions. `feedback_count` should increase,
   `online` should be 1, and `rotor_rpm` should rise toward 1000.

Alternatively, build in PowerShell with `./build.ps1`. It finds ARM GCC on PATH
or under `C:/ST`; pass `-ToolchainBin 'path/to/bin'` for another installation.
Outputs are `build/motor_test.elf` and `build/motor_test.bin`. The binary's flash
address is **0x08000000**. Building does not flash the board.

Change settings in `Core/Inc/motor_test.h`:

| Setting | Default | Meaning |
| --- | --- | --- |
| MOTOR_ESC_ID | 1 | C620 ID, 1 through 8 |
| MOTOR_TARGET_ROTOR_RPM | 1000 | Motor rotor speed, approximately 53 RPM at the P19 output |
| MOTOR_CURRENT_LIMIT | 2000 | Raw current cap, approximately 2.44 A |
| MOTOR_SPEED_RAMP_RPM_S | 500 | Rotor speed ramp |
| MOTOR_FEEDBACK_TIMEOUT_MS | 100 | Feedback age limit |

The speed PI gains are starting values for an unloaded bench test; loaded
mechanisms may need tuning. If a motor does not turn, inspect feedback and wiring
before increasing the current limit. The command target is clamped to +/-5000
rotor RPM in the test application.

While running, edit `motor_target_rpm` to change speed (negative reverses direction).
Set `motor_enabled = 0` or `motor_target_rpm = 0` to send zero current and let the
motor coast. Setting `motor_enabled` back to 1 resumes the ramp. Do not rely on a
debugger halt as a stop command; use the C620 power disconnect for a physical stop.

The firmware sends a current command approximately every 1 ms. It decodes angle,
signed rotor RPM, signed measured current, and temperature. Before startup,
when disabled, after a scheduling gap over 10 ms, or when feedback is stale,
it resets the PI and sends zero current. Bus-off recovery is enabled. With a
broken bus, zero-current frames cannot reach the ESC. Communication restoration
automatically resumes the ramp if enabled. `tx_count` counts frames queued into
the CAN controller, not confirmed successful deliveries. `can_error` is the HAL's
accumulated error flags; `online` uses recent valid feedback and live bus-off state.

CAN1 uses PD0/PD1 at 1 Mbit/s. Standard 8-byte commands use 0x200 for IDs 1-4
or 0x1FF for IDs 5-8; feedback is 0x200 + ESC ID. All other motor slots in the
selected command group are sent as zero: this is a **single-motor test** and must
be the only command producer for that group.
Protocol reference: [DJI C620 user guide](https://cdn-hz.robomaster.com/robomasters/public/document/RoboMaster%20C620%20Brushless%20DC%20Motor%20Speed%20Controller%20V1.01.pdf).

The original robot project is preserved in `archive/original_robot_project.zip`;
removed files are also available in `archive/legacy`. Neither is compiled.
The original CubeMX `.ioc` is archived because it generates the full robot
application. This minimal project is maintained as C source; do not regenerate
it using the archived `.ioc`.

Build verification checks compilation/linking only. Motor rotation and speed
control must still be verified on the connected hardware.
