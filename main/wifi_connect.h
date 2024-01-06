#pragma once
#include <stdint.h>

// should this be in current_state instead?
typedef enum { DISCONNECTED, CONNECTING, CONNECTED } wifi_status_t;
extern int8_t wifi_rssi;
extern wifi_status_t wifi_status;

void wifi_connect(void* params);