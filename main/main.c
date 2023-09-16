#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_connect.c"

void app_main(void) {
  // initialie NVS
  esp_err_t ret = nvs_flash_init();
  // read wifi password from NVS

  nvs_handle_t nvs_handle;
  ret = nvs_open("device_config", NVS_READONLY, &nvs_handle);
  ESP_ERROR_CHECK(ret);
  size_t required_size;
  nvs_get_str(nvs_handle, "wifi_ssid", NULL, &required_size);
  char* wifi_ssid = malloc(required_size);
  nvs_get_str(nvs_handle, "wifi_ssid", wifi_ssid, &required_size);
  nvs_close(nvs_handle);
  printf("wifi_ssid: %s\n", wifi_ssid);

  xTaskCreate(&wifi_connect, "wifi_connect", 4096, NULL, 5, NULL);
}
