#pragma once

typedef enum { Disconnected, Connecting, Connected } wifi_connectivity_t;
extern wifi_connectivity_t wifi_connectivity_status;
void wifi_connect(void* params);