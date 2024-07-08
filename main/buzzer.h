#pragma once

typedef enum { BEEP_SHORT, BEEP_LONG, BATTERY_EMPTY, STARTUP, POWER_CONNECTED } beep_type_t;

typedef enum __attribute__((packed)) { SILENT_MODE_OFF, SILENT_MODE_ON } silent_mode_t;

extern silent_mode_t silent_mode;

void buzzer(void* params);
void trigger_beep(beep_type_t type);
void toggle_silent_mode();