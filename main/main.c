#include <stdio.h>
#include "esp_event.h"
#include "esp_log.h"
#include "fetch_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_connect.h"

ESP_EVENT_DEFINE_BASE(USER_EVENT_BASE);

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  xTaskCreate(&wifi_connect, "wifi_connect", 4096, NULL, 5, NULL);
  xTaskCreate(&fetch_config, "fetch_config", 4096, NULL, 5, NULL);
}
