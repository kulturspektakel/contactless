#pragma once

#define BATTERY_MAX 2060
#define BATTERY_MIN 1500

extern int battery_voltage;
extern int usb_voltage;
void power_management(void* params);