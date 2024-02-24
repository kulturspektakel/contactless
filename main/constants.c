#include "constants.h"
#include <string.h>
#include "esp_efuse_custom_table.h"
#include "esp_mac.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

const char SALT[SALT_LENGTH + 1];
const char DEVICE_ID[DEVICE_ID_LENGTH + 1];
const char* LOG_DIR = "/littlefs/logs";
const char* CONFIG_FILE = "/littlefs/config.cfg";

static const char* TAG = "constants";

void load_device_id(void* params) {
  size_t size = esp_efuse_get_field_size(ESP_EFUSE_DEVICE_ID) / 8;
  if (size > DEVICE_ID_LENGTH) {
    ESP_LOGE(TAG, "Invalid device id size: %d", size);
    vTaskDelete(NULL);
    return;
  }
  ESP_ERROR_CHECK(esp_efuse_read_field_blob(ESP_EFUSE_DEVICE_ID, DEVICE_ID, DEVICE_ID_LENGTH * 8));
  ESP_LOG_BUFFER_HEX(TAG, DEVICE_ID, DEVICE_ID_LENGTH);
  size_t len = strlen(DEVICE_ID);
  if (len == 0) {
    ESP_LOGE(TAG, "Invalid device id");
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "Device ID: %s", DEVICE_ID);

  xEventGroupSetBits(event_group, DEVICE_ID_LOADED);
  vTaskDelete(NULL);
}

void load_salt(void* params) {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_DEVICE_CONFIG, NVS_READONLY, &nvs_handle));
  size_t salt_size;
  nvs_get_str(nvs_handle, NVS_SALT, NULL, &salt_size);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_SALT, SALT, &salt_size));
  nvs_close(nvs_handle);
  xEventGroupSetBits(event_group, SALT_LOADED);
  vTaskDelete(NULL);
}
