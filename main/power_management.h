#pragma once

#define USB_VOLTAGE_THRESHOLD 1000

extern int battery_voltage;
extern int usb_voltage;
int battery_percentage();
void power_management(void* params);
void reset_power_off_timer();