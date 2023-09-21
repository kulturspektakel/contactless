#include "fetch_config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "event_system.h"
#include "http_event_handler.h"
#include "pb_decode.h"

ESP_EVENT_DECLARE_BASE(USER_EVENT_BASE);

static const char* TAG = "fetch_config";

static void event_handler(void* arg) {
  xTaskNotifyGive(arg);
}

void fetch_config(void* params) {
  esp_event_handler_register(
      USER_EVENT_BASE, WIFI_CONNECTED, &event_handler, xTaskGetCurrentTaskHandle()
  );
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  ESP_LOGI(TAG, "start fetching config");

  char local_response_buffer[2048] = {0};

  esp_http_client_config_t config = {
      .host = "api.kulturspektakel.de",
      .path = "/configs",
      .event_handler = http_event_handler,
      .user_data = local_response_buffer,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  // GET
  esp_err_t err = esp_http_client_perform(client);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "%s", local_response_buffer);
    ESP_LOGI(
        TAG,
        "HTTP GET Status = %d, content_length = %" PRId64,
        esp_http_client_get_status_code(client),
        esp_http_client_get_content_length(client)
    );

  } else {
    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
  }
  esp_http_client_cleanup(client);
  vTaskDelete(NULL);
}
