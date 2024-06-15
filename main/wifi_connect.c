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
#include "power_management.h"

static TimerHandle_t update_timer;
int8_t wifi_rssi = INT8_MIN;
wifi_status_t wifi_status = DISCONNECTED;

static void update_signal_strength() {
  wifi_ap_record_t wifidata;
  esp_wifi_sta_get_ap_info(&wifidata);
  wifi_rssi = wifidata.rssi;
  xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
}

static int timer_duration() {
  bool is_connected = xEventGroupGetBits(event_group) & WIFI_CONNECTED;
  bool usb_connected = xEventGroupGetBits(event_group) & USB_CONNECTED;
  if (is_connected) {
    // update signal strength every 10 seconds
    return 10000;
  } else if (usb_connected) {
    // try reconnecting every minute when on battery
    return 60000;
  } else {
    // try reconnecting every 3 minutes when on power
    return 60000 * 3;
  }
}

static void timer_cb(TimerHandle_t timer) {
  bool is_connected = xEventGroupGetBits(event_group) & WIFI_CONNECTED;
  if (is_connected) {
    update_signal_strength();
  } else {
    xTaskNotifyGive(xTaskGetCurrentTaskHandle());
  }
  xTimerChangePeriod(update_timer, pdMS_TO_TICKS(timer_duration()), 0);
  xTimerReset(update_timer, 0);
}

static void clearTimer() {
  if (update_timer != NULL) {
    xTimerDelete(update_timer, 0);
    update_timer = NULL;
  }
}

static void startTimer() {
  clearTimer();
  update_timer =
      xTimerCreate("update_timer", pdMS_TO_TICKS(timer_duration()), pdFALSE, 0, timer_cb);
  xTimerStart(update_timer, 0);
}

static void event_handler(
    void* arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data
) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(WIFI_CONNECT_TASK, "Trying to connect to WiFi...");
    clearTimer();
    wifi_status = CONNECTING;
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(WIFI_CONNECT_TASK, "WiFi disconnected");
    wifi_status = DISCONNECTED;
    xEventGroupClearBits(event_group, WIFI_CONNECTED);
    startTimer();  // timer for reconnecting
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(WIFI_CONNECT_TASK, "WiFi connected");
    wifi_status = CONNECTED;
    update_signal_strength();
    xEventGroupSetBits(event_group, WIFI_CONNECTED);
    startTimer();  // timer for signal strength
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
      WIFI_CONNECT_TASK,
      "initialized with ssid=%s password=%s",
      wifi_config.sta.ssid,
      wifi_config.sta.password
  );

  while (1) {
    // reconnect if disconnected
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (wifi_status == DISCONNECTED) {
      esp_wifi_connect();
    }
  }
}
