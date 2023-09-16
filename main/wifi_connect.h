#pragma once

enum WiFiState { Disconnected, Connecting, Connected };

enum WiFiState wifi_state;

void wifi_connect(void* params);