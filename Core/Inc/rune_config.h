#ifndef RUNE_CONFIG_H
#define RUNE_CONFIG_H
/* Editable EXAMPLE mechanical values. Verify before powering a mechanism. */
#define RUNE_GEARBOX_RATIO       19.0f
#define RUNE_MOTOR_PULLEY_TEETH   20.0f
#define RUNE_OUTPUT_PULLEY_TEETH  60.0f
#define RUNE_MAX_RPM             20.0f
#define RUNE_DEFAULT_RPM_X10     100
#define RUNE_LEASE_TIMEOUT_MS    2000U
#define RUNE_REQUIRE_ALL_BLADES  1
/* 0 = main controller. Build blade firmware with -DUREX_BLADE_ID=1..5. */
#ifndef UREX_BLADE_ID
#define UREX_BLADE_ID 0
#endif
/* Arrow, upper, lower, central target, outer border. Placeholders, edit these. */
#define BLADE_LED_COUNTS         {30, 30, 30, 30, 30}
#define BLADE_MAX_LEDS_PER_CHAIN 256U
#define BLADE_BRIGHTNESS_LIMIT   64U
#if UREX_BLADE_ID < 0 || UREX_BLADE_ID > 5
#error "UREX_BLADE_ID must be 0 (main) or 1..5 (blade)"
#endif
#endif
