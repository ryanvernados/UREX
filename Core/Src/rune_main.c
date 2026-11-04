#include "rune_app.h"
#include "rune_uart.h"
#include "motor_test.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#if UREX_BLADE_ID == 0
typedef struct { uint32_t boot, seen; uint16_t hits; uint8_t state, ring, seq; } Blade;
typedef struct { uint32_t id, run, at; uint8_t blade, ring; } Event;
static Blade blades[5];
static Event events[32];
static uint8_t commands[5][8], event_head, event_tail, active, completed;
static uint8_t running, bench, direction = 1, motion_mode = 1;
static uint16_t rpm_x10 = RUNE_DEFAULT_RPM_X10;
static uint32_t boot_id, run_id, next_event, lease_at, run_at;
static uint32_t known_commands[16];
static char known_results[16][32];
static uint8_t known_count, known_next, have_lease;
static const char *stop_reason = "boot_stopped";
static uint8_t online(unsigned i, uint32_t now) { return blades[i].boot && now - blades[i].seen < 500; }
static uint8_t lease_valid(uint32_t now) { return have_lease && now - lease_at < RUNE_LEASE_TIMEOUT_MS; }
static void blade_command(unsigned i, unsigned mode)
{
    ++commands[i][1]; commands[i][2] = mode;
}
static void stop(const char *reason)
{
    running = bench = 0; motor_enabled = 0; motor_target_rpm = 0; active = 0;
    stop_reason = reason;
    for (unsigned i = 0; i < 5; ++i) { blade_command(i, 0); }
}
static void select_target(unsigned i)
{
    active = i + 1;
    blade_command(i, 2);
}
static void acknowledge(uint32_t id, const char *result)
{
    char json[180];
    snprintf(json, sizeof(json), "{\"kind\":\"ack\",\"boot_id\":%lu,\"id\":%lu,\"result\":\"%s\"}",
             (unsigned long)boot_id, (unsigned long)id, result);
    Rune_UART_Send(json);
}
static const char *apply_command(unsigned action, unsigned mode, unsigned speed, unsigned dir, unsigned blade, uint32_t now)
{
    if (action == 0) { stop("operator_stop"); return "applied"; }
    if (!lease_valid(now)) { return "control_lease_expired"; }
    if (action == 4) {
        if (running || bench) { return "stop_before_reset"; }
        ++run_id; completed = 0; return "applied";
    }
    if (mode > 2 || speed > (unsigned)(RUNE_MAX_RPM * 10) || dir > 1) { return "invalid_settings"; }
    if (action == 2) {
        motion_mode = mode; rpm_x10 = speed; direction = dir;
        return "applied";
    }
    if (action == 3) {
        if (running) { return "stop_before_blade_test"; }
        if ((event_head + 1) % 32 == event_tail) { return "wait_for_hit_delivery"; }
        if (blade < 1 || blade > 5 || !online(blade - 1, now)) { return "blade_offline"; }
        stop("blade_test"); ++run_id; bench = 1; completed = 0;
        select_target(blade - 1); return "applied";
    }
    if (action != 1) { return "unknown_action"; }
    if (running || bench) { return "already_active"; }
    if (!motor_status.online) { return "motor_offline"; }
    uint8_t mask = 0;
    for (unsigned i = 0; i < 5; ++i) { if (online(i, now)) { mask |= 1U << i; } }
    if (!mask || (RUNE_REQUIRE_ALL_BLADES && mask != 31)) { return "missing_blades"; }
    if (event_head != event_tail) { return "wait_for_hit_delivery"; }
    motion_mode = mode; rpm_x10 = speed; direction = dir;
    ++run_id; completed = 0; running = 1; bench = 0; run_at = now;
    stop_reason = "running";
    for (unsigned i = 0; i < 5; ++i) { blade_command(i, 1); }
    unsigned first = Rune_Random32() % 5;
    while (!(mask & (1U << first))) { first = (first + 1) % 5; }
    select_target(first);
    return "applied";
}
static void read_uart(uint32_t now)
{
    char line[320];
    for (unsigned n = 0; n < 8 && Rune_UART_Line(line, sizeof(line)); ++n) {
        unsigned long boot, id;
        int consumed = 0;
        if (sscanf(line, "L %lu%n", &boot, &consumed) == 1 && line[consumed] == 0) {
            if (boot == boot_id) { have_lease = 1; lease_at = now; }
        } else if (sscanf(line, "A %lu %lu%n", &boot, &id, &consumed) == 2 && line[consumed] == 0) {
            if (boot == boot_id && event_tail != event_head && events[event_tail].id == id) {
                event_tail = (event_tail + 1) % 32;
            }
        } else {
            unsigned action, mode, speed, dir, blade;
            if (sscanf(line, "C %lu %lu %u %u %u %u %u%n", &id, &boot, &action, &mode,
                       &speed, &dir, &blade, &consumed) != 7 || line[consumed] != 0) { continue; }
            if (boot != boot_id) { acknowledge(id, "stale_controller_boot"); continue; }
            unsigned found = 16;
            for (unsigned i = 0; i < known_count; ++i) { if (known_commands[i] == id) { found = i; break; } }
            if (found != 16) { acknowledge(id, known_results[found]); continue; }
            const char *result = apply_command(action, mode, speed, dir, blade, now);
            known_commands[known_next] = id;
            snprintf(known_results[known_next], sizeof(known_results[0]), "%s", result);
            known_next = (known_next + 1) % 16;
            if (known_count < 16) { ++known_count; }
            acknowledge(id, result);
        }
    }
}
static void record_hit(unsigned i, uint8_t ring, uint32_t now)
{
    uint8_t next = (event_head + 1) % 32;
    if (next == event_tail) { stop("event_buffer_full"); return; }
    events[event_head] = (Event){++next_event, run_id, now, i + 1, ring};
    event_head = next;
    if ((event_head + 1) % 32 == event_tail) { stop("event_buffer_full"); return; }
    completed |= 1U << i;
    blade_command(i, 3);
    if (bench) { active = 0; return; }
    uint8_t remaining = 0;
    for (unsigned j = 0; j < 5; ++j) {
        if (!(completed & (1U << j)) && online(j, now)) { remaining |= 1U << j; }
    }
    if (!remaining) { stop("cycle_complete"); return; }
    unsigned pick = Rune_Random32() % 5;
    while (!(remaining & (1U << pick))) { pick = (pick + 1) % 5; }
    select_target(pick);
}
static void read_can(uint32_t now)
{
    for (unsigned n = 0; n < 32 && HAL_CAN_GetRxFifoFillLevel(&rune_can, CAN_RX_FIFO0); ++n) {
        CAN_RxHeaderTypeDef rx; uint8_t d[8];
        if (HAL_CAN_GetRxMessage(&rune_can, CAN_RX_FIFO0, &rx, d) != HAL_OK) { break; }
        if (rx.IDE != CAN_ID_STD || rx.RTR != CAN_RTR_DATA || rx.DLC != 8 || d[0] != 1) { continue; }
        if (rx.StdId >= 0x3C1 && rx.StdId <= 0x3C5) {
            unsigned i = rx.StdId - 0x3C1;
            uint32_t boot = d[1] | ((uint32_t)d[2] << 8) | ((uint32_t)d[3] << 16) | ((uint32_t)d[4] << 24);
            if (boot && blades[i].boot != boot) {
                if (running || bench) { stop("blade_restarted"); }
                blades[i].boot = boot; blades[i].hits = 0;
            }
        } else if (rx.StdId >= 0x381 && rx.StdId <= 0x385) {
            unsigned i = rx.StdId - 0x381;
            if (!blades[i].boot) { continue; }
            uint16_t hits = d[6] | ((uint16_t)d[7] << 8);
            uint8_t changed = hits != blades[i].hits;
            blades[i].seen = now; blades[i].seq = d[1]; blades[i].state = d[2]; blades[i].ring = d[3];
            blades[i].hits = hits;
            if (changed && (running || bench) && active == i + 1 && d[1] == commands[i][1] &&
                (d[2] & 0x13) == 3 && d[3] >= 1 && d[3] <= 10) { record_hit(i, d[3], now); }
        }
    }
}
void Rune_Init(void)
{
    motor_enabled = 0; motor_target_rpm = 0;
    boot_id = Rune_Random32();
    Rune_UART_Init(); Rune_CAN_Init();
    for (unsigned i = 0; i < 5; ++i) {
        commands[i][0] = 1; commands[i][3] = 32; commands[i][4] = 255;
    }
}
void Rune_Tick1ms(void) {}
void Rune_Update(void)
{
    uint32_t now = HAL_GetTick();
    read_uart(now); read_can(now);
    if ((running || bench) && !lease_valid(now)) { stop("control_link_lost"); }
    if (running && !motor_status.online) { stop("motor_feedback_lost"); }
    if (running && RUNE_REQUIRE_ALL_BLADES) {
        for (unsigned i = 0; i < 5; ++i) { if (!online(i, now)) { stop("blade_link_lost"); break; } }
    }
    if ((running || bench) && active && !online(active - 1, now)) { stop("active_blade_lost"); }
    if (running) {
        float rpm = rpm_x10 * 0.1f;
        if (motion_mode == 1) { rpm = 10.0f; }
        if (motion_mode == 2) { rpm = (0.9f * sinf(1.94f * (now - run_at) * 0.001f) + 1.19f) * 9.5492966f; }
        if (rpm > RUNE_MAX_RPM) { rpm = RUNE_MAX_RPM; }
        float rotor = rpm * RUNE_GEARBOX_RATIO * RUNE_OUTPUT_PULLEY_TEETH / RUNE_MOTOR_PULLEY_TEETH;
        motor_target_rpm = (int32_t)(direction ? rotor : -rotor);
        motor_enabled = 1;
    } else { motor_enabled = 0; motor_target_rpm = 0; }
    static uint32_t send_at, telemetry_at, event_at;
    static uint8_t next_blade;
    if (now - send_at >= 20) {
        if (Rune_CAN_Send(0x301 + next_blade, commands[next_blade]) == HAL_OK) {
            next_blade = (next_blade + 1) % 5; send_at = now;
        }
    }
    if (event_head != event_tail && now - event_at >= 500) {
        Event *e = &events[event_tail]; char json[260];
        snprintf(json, sizeof(json), "{\"kind\":\"hit\",\"boot_id\":%lu,\"session_id\":\"%lu-%lu\",\"event_id\":%lu,\"blade_id\":%u,\"ring\":%u,\"time_ms\":%lu}",
                 (unsigned long)boot_id, (unsigned long)boot_id, (unsigned long)e->run,
                 (unsigned long)e->id, e->blade, e->ring, (unsigned long)e->at);
        if (Rune_UART_Send(json)) { event_at = now; }
    }
    if (now - telemetry_at >= 250) {
        char json[1000]; uint8_t mask = 0;
        for (unsigned i = 0; i < 5; ++i) { if (online(i, now)) { mask |= 1U << i; } }
        int actual = (int)(motor_status.rotor_rpm * 10.0f * RUNE_MOTOR_PULLEY_TEETH / (RUNE_GEARBOX_RATIO * RUNE_OUTPUT_PULLEY_TEETH));
        snprintf(json, sizeof(json), "{\"kind\":\"telemetry\",\"boot_id\":%lu,\"session_id\":\"%lu-%lu\",\"running\":%s,\"bench\":%s,\"mode\":%u,\"set_rpm_x10\":%u,\"rune_rpm_x10\":%d,\"rotor_rpm\":%d,\"direction\":%u,\"motor_online\":%s,\"lease\":%s,\"blade_mask\":%u,\"active_blade\":%u,\"blade_states\":[%u,%u,%u,%u,%u],\"blade_rings\":[%u,%u,%u,%u,%u],\"buffered_events\":%u,\"reason\":\"%s\"}",
                 (unsigned long)boot_id, (unsigned long)boot_id, (unsigned long)run_id,
                 running ? "true" : "false", bench ? "true" : "false", motion_mode, rpm_x10, actual,
                 motor_status.rotor_rpm, direction, motor_status.online ? "true" : "false", lease_valid(now) ? "true" : "false",
                 mask, active, blades[0].state, blades[1].state, blades[2].state, blades[3].state, blades[4].state,
                 blades[0].ring, blades[1].ring, blades[2].ring, blades[3].ring, blades[4].ring,
                 (event_head + 32 - event_tail) % 32, stop_reason);
        if (Rune_UART_Send(json)) { telemetry_at = now; }
    }
}
#endif
