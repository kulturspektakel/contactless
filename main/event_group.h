#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define LOCAL_CONFIG_LOADED BIT0
#define REMOTE_CONFIG_FETCHED BIT1
#define WIFI_CONNECTED BIT2
#define WIFI_CONNECTING BIT3
#define LOCAL_CONFIG_UPDATED BIT4

extern EventGroupHandle_t event_group;
