#pragma once
#include <stdint.h>

// UREX protocol v1. This is NOT Shenzhen's original wire protocol.
namespace BladeProtocol {
constexpr uint8_t VERSION = 1;
constexpr uint32_t COMMAND_BASE = 0x300;
constexpr uint32_t STATUS_BASE = 0x380;
enum Mode : uint8_t { OFF = 0, IDLE = 1, TARGET = 2, HIT = 3 };
// Command: version, sequence, mode, brightness, red, green, blue, reserved=0.
// Status: version, sequence, state/flags, last ring, mask L, mask H,
//         hit counter L, hit counter H. Integers are little-endian.
constexpr uint8_t FLAG_TIMEOUT = 0x10;
constexpr uint8_t FLAG_BENCH = 0x20;
constexpr uint8_t FLAG_EVENT_OVERFLOW = 0x40;
}
