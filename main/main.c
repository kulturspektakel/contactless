#include <stdio.h>
#include "buzzer.h"
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
#include "log_writer.h"
#include "nvs_flash.h"
#include "power_management.h"
#include "rfid.h"
#include "state_machine.h"
#include "time_sync.h"
#include "wifi_connect.h"

EventGroupHandle_t event_group;

#define PRIO_NORMAL 5
#define PRIO_HIGH 10

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

  xTaskCreate(&wifi_connect, "wifi_connect", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&local_config, "local_config", 5120, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&fetch_config, "fetch_config", 16096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&log_uploader, "log_uploader", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&display, "display", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&keypad, "keypad", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&log_writer, "log_writer", 4096, NULL, PRIO_NORMAL, NULL);
  // state machine needs to run at a higher priority than the other tasks, so that other tasks can
  // yield for the state to update
  xTaskCreate(&state_machine, "state_machine", 4096, NULL, PRIO_HIGH, NULL);
  xTaskCreate(&time_sync, "time_sync", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&rfid, "rfid", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&power_management, "power_management", 4096, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&load_device_id, "load_device_id", 3072, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&load_salt, "load_salt", 3072, NULL, PRIO_NORMAL, NULL);
  xTaskCreate(&buzzer, "buzzer", 5120, NULL, PRIO_NORMAL, NULL);
}
