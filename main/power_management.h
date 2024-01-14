#pragma once

extern int battery_voltage;
extern int usb_voltage;
int battery_percentage();
void power_management(void* params);