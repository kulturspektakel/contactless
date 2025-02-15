#pragma once

typedef enum {
  BEEP_SHORT,
  BEEP_LONG,
  STARTUP,
  POWER_CONNECTED,
  KEY_PRESS,

  _BEEP_COUNT
} beep_type_t;

typedef enum __attribute__((packed)) {
  SOUND_MODE_DEFAULT,
  SOUND_MODE_SILENT,
  SOUND_MODE_KEYPRESS,
  _SOUND_MODE_COUNT
} sound_mode_t;

extern sound_mode_t sound_mode;

void buzzer(void* params);
void trigger_beep(beep_type_t type);
void trigger_forced_beep(beep_type_t type);
void toggle_sound_mode();