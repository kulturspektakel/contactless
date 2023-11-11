#include "time_sync.h"
#include <sys/time.h>
#include <time.h>
#include "ds3231.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "time_sync";

void time_sync(void* params) {
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  ESP_ERROR_CHECK(i2cdev_init());
  i2c_dev_t dev = {
      .addr = DS3231_ADDR,
  };
  ESP_ERROR_CHECK(ds3231_init_desc(&dev, I2C_NUM_0, 5, 17));

  struct tm rtc_time;
  ds3231_get_time(&dev, &rtc_time);

  if (rtc_time.tm_year >= (2023 - 1900)) {
    // update system time
    struct timeval tv = {.tv_sec = mktime(&rtc_time)};
    settimeofday(&tv, NULL);
    xEventGroupSetBits(event_group, TIME_SET);
    ESP_LOGI(TAG, "RTC time seems valid, setting system time to %s", asctime(&rtc_time));
  } else {
    ESP_LOGI(TAG, "RTC time is invalid: %s. Waiting for NTP.", asctime(&rtc_time));
  }

  // wait for wifi to connect
  xEventGroupWaitBits(event_group, WIFI_CONNECTED, false, true, portMAX_DELAY);

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&config);

  for (int retries = 10; retries > 0; retries--) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retries);
    if (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) == ESP_OK) {
      xEventGroupSetBits(event_group, TIME_SET);

      // time acquired, update RTC
      time_t now;
      time(&now);
      struct tm* timeinfo = gmtime(&now);
      ESP_LOGI(TAG, "Received NTP time. Setting RTC to %s UTC", asctime(timeinfo));
      ESP_ERROR_CHECK_WITHOUT_ABORT(ds3231_set_time(&dev, timeinfo));
      break;
    }
  }

  ds3231_free_desc(&dev);
  esp_netif_sntp_deinit();
  ESP_LOGI(TAG, "Done, terminating task");
  vTaskDelete(NULL);
}