#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Network requests take a lot of memory, mostly because of the SSL handshake.
// To avoid running out of memory, we will use a semaphore to limit the number of concurrent network
extern SemaphoreHandle_t network_request;