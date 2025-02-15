#pragma once

#include <stdbool.h>

extern int battery_voltage;
extern int usb_voltage;
bool battery_is_low();
int battery_percentage();
void power_management(void* params);
void reset_power_off_timer();