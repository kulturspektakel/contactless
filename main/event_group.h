#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define LOCAL_CONFIG_LOADED BIT0
#define REMOTE_CONFIG_FETCHED BIT1
#define WIFI_CONNECTED BIT2
#define LOCAL_CONFIG_UPDATED BIT3
#define TIME_SET BIT4
#define DISPLAY_NEEDS_UPDATE BIT5

extern EventGroupHandle_t event_group;
