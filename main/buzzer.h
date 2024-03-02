#pragma once

typedef enum { BEEP_SHORT, BEEP_LONG } beep_type_t;

void buzzer(void* params);
void trigger_beep(beep_type_t type);