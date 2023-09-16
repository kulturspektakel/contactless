#include "wifi_connect.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "wifi_connect";

static void connect_to_wifi() {
  wifi_state = Connecting;
  esp_wifi_connect();
}

static void event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    connect_to_wifi();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_state = Disconnected;
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    wifi_state = Connected;
  }
  xTaskNotifyGive(arg);
}

void wifi_connect(void* params) {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
  esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, current_task, &instance_any_id
  );
  esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, current_task, &instance_got_ip
  );

  wifi_config_t wifi_config = {
      .sta = {.ssid = "", .password = ""},
  };

  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open("device_config", NVS_READONLY, &nvs_handle));

  size_t required_size;
  nvs_get_str(nvs_handle, "wifi_ssid", NULL, &required_size);
  nvs_get_str(nvs_handle, "wifi_ssid", &wifi_config.sta.ssid, &required_size);
  nvs_get_str(nvs_handle, "wifi_password", NULL, &required_size);
  nvs_get_str(nvs_handle, "wifi_password", &wifi_config.sta.password, &required_size);
  nvs_close(nvs_handle);

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  esp_wifi_start();

  ESP_LOGI(
      TAG, "initialized with ssid=%s password=%s", wifi_config.sta.ssid, wifi_config.sta.password
  );

  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "state changed: %d", wifi_state);
    if (wifi_state == Disconnected) {
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      connect_to_wifi();
    }
  }
}
