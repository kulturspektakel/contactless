#pragma once

typedef enum {
  BEEP_SHORT,
  BEEP_LONG,
  BATTERY_EMPTY,
  STARTUP,
  POWER_CONNECTED,
  KEY_PRESS
} beep_type_t;

typedef enum __attribute__((packed)) {
  SILENT_MODE_OFF,
  SILENT_MODE_ON,
  SILENT_MODE_OFF_WITH_KEYPRESS,
  _SILENT_MODE_COUNT
} silent_mode_t;

extern silent_mode_t silent_mode;

void buzzer(void* params);
void trigger_beep(beep_type_t type);
void toggle_silent_mode();