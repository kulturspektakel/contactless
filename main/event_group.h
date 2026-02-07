#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define LOCAL_CONFIG_LOADED BIT0
#define READY_TO_FETCH_CONFIG BIT1
#define WIFI_CONNECTED BIT2
#define TIME_SET BIT3
#define DISPLAY_NEEDS_UPDATE BIT4
#define DEVICE_ID_LOADED BIT5
#define SALT_LOADED BIT6
#define USB_CONNECTED BIT7
#define CONFIG_UPDATE_PENDING BIT8

static const EventBits_t STARTUP_BITS =
    LOCAL_CONFIG_LOADED | TIME_SET | DEVICE_ID_LOADED | SALT_LOADED;

extern EventGroupHandle_t event_group;
