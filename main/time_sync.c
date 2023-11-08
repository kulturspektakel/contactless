#include "time_sync.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time.h"

static const char* TAG = "time_sync";

void time_sync(void* params) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  // TODO read from RTC

  // wait for wifi to connect
  xEventGroupWaitBits(event_group, WIFI_CONNECTED, false, true, portMAX_DELAY);

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);

  // wait for time to be set
  int retries = 15;
  while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_ERR_TIMEOUT && retries-- > 0) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retries);
  }
  esp_netif_sntp_deinit();

  // TODO set RTC

  vTaskDelete(NULL);
}