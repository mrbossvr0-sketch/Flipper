#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pure logic: compiles both on desktop and on Flipper. No bus writes here. */
typedef struct {
    int rpm;
    int speed_kmh;
    int coolant_c;
    bool rpm_ok;
    bool speed_ok;
    bool coolant_ok;
} ObdLive;

/* mode-01 read-only PID response parser: 41 0C, 41 0D, 41 05. */
bool obd_parse_pid_response(const char* raw, uint8_t pid, ObdLive* live);
