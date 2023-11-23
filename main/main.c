#include <stdio.h>
#include "display.h"
#include "esp_event.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "event_group.h"
#include "fetch_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keypad.h"
#include "local_config.h"
#include "log_uploader.h"
#include "nvs_flash.h"
#include "state_machine.h"
#include "time_sync.h"
#include "wifi_connect.h"

EventGroupHandle_t event_group;

void app_main(void) {
  event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(nvs_flash_init());
  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "littlefs",
      .format_if_mount_failed = true,
      .dont_mount = false,
  };
  ESP_ERROR_CHECK(esp_vfs_littlefs_register(&conf));

  xTaskCreate(&wifi_connect, "wifi_connect", 4096, NULL, 5, NULL);
  xTaskCreate(&local_config, "local_config", 4096, NULL, 5, NULL);
  xTaskCreate(&fetch_config, "fetch_config", 16096, NULL, 5, NULL);
  xTaskCreate(&log_uploader, "log_uploader", 4096, NULL, 5, NULL);
  xTaskCreate(&display, "display", 4096, NULL, 5, NULL);
  xTaskCreate(&keypad, "keypad", 4096, NULL, 5, NULL);
  xTaskCreate(&state_machine, "state_machine", 4096, NULL, 5, NULL);
  // xTaskCreate(&time_sync, "time_sync", 4096, NULL, 5, NULL);
}
