#pragma once

#define BATTERY_MAX 2070
#define BATTERY_MIN 1500

extern int battery_voltage;
extern int usb_voltage;
void power_management(void* params);