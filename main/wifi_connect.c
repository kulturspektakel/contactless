#include "wifi_connect.h"
#include "constants.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* TAG = "wifi_connect";
static TimerHandle_t signal_strength_timer;
static int backoff_counter = 1;
int8_t wifi_rssi = 0;
wifi_status_t wifi_status = DISCONNECTED;

static void update_signal_strength(TimerHandle_t timer) {
  wifi_ap_record_t wifidata;
  esp_wifi_sta_get_ap_info(&wifidata);
  wifi_rssi = wifidata.rssi;
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

static void event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    if (signal_strength_timer != NULL) {
      xTimerDelete(signal_strength_timer, 0);
    }
    wifi_status = CONNECTING;
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_status = DISCONNECTED;
    xEventGroupClearBits(event_group, WIFI_CONNECTED);
    if (signal_strength_timer != NULL) {
      xTimerDelete(signal_strength_timer, 0);
    }
    if (backoff_counter < 15) {
      backoff_counter++;
    }
    // notify task to try reconnecting
    xTaskNotifyGive(arg);
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    backoff_counter = 1;
    wifi_status = CONNECTED;
    xEventGroupSetBits(event_group, WIFI_CONNECTED);
    signal_strength_timer = xTimerCreate(
        "wifi_signal_strength", pdMS_TO_TICKS(30000), pdTRUE, 0, update_signal_strength
    );
  }
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
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
  ESP_ERROR_CHECK(nvs_open(NVS_DEVICE_CONFIG, NVS_READONLY, &nvs_handle));

  size_t required_size;
  nvs_get_str(nvs_handle, NVS_WIFI_SSID, NULL, &required_size);
  nvs_get_str(nvs_handle, NVS_WIFI_SSID, &wifi_config.sta.ssid, &required_size);
  nvs_get_str(nvs_handle, NVS_WIFI_PASSWORD, NULL, &required_size);
  nvs_get_str(nvs_handle, NVS_WIFI_PASSWORD, &wifi_config.sta.password, &required_size);
  nvs_close(nvs_handle);

  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  esp_wifi_start();

  ESP_LOGI(
      TAG, "initialized with ssid=%s password=%s", wifi_config.sta.ssid, wifi_config.sta.password
  );

  while (1) {
    // reconnect if disconnected
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Disconnected, reconnecting in %d minutes", backoff_counter);
    vTaskDelay(pdMS_TO_TICKS(60000 * backoff_counter));
    esp_wifi_connect();
  }
}
