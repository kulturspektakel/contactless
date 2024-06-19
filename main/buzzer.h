#pragma once

typedef enum { BEEP_SHORT, BEEP_LONG, BATTERY_EMPTY, STARTUP } beep_type_t;

void buzzer(void* params);
void trigger_beep(beep_type_t type);