#include "fetch_config.h"
#include "configs.pb.h"
#include "constants.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "event_group.h"
#include "freertos/timers.h"
#include "http_auth_headers.h"
#include "local_config.h"
#include "network_request.h"
#include "pb_decode.h"

#define CONFIG_CHECK_INTERVAL_MS (15 * 60 * 1000)  // 15 minutes

// Upper bound on the config response body. We accumulate ON_DATA chunks into a
// fixed buffer of this size instead of trusting Content-Length, so chunked
// (Transfer-Encoding: chunked, Content-Length: -1) responses work too.
//
// The live /api/kultcash/lists payload measured ~3.2 KB: 17 product lists
// (84-388 B each), privilege_tokens already at its 30-entry cap, 0 suspended
// crew cards. Size scales with the number of product lists (the server sends
// them all and the firmware keeps the selected one); the two bytes arrays are
// capped at 30x7 B each. Expected growth is ~7 more lists and crew cards up to
// 30, which projects to ~6 KB. 16 KB leaves ample headroom even if new lists
// are far larger than today's; it's a transient alloc freed after each fetch.
#define MAX_CONFIG_RESPONSE_SIZE 16384

static size_t bytes_written = 0;
static uint8_t* buffer = NULL;
static TimerHandle_t periodic_config_timer = NULL;

esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      bytes_written = 0;
      break;

    case HTTP_EVENT_ON_DATA:
      // Accumulate the body across however many ON_DATA callbacks arrive. This
      // handles both Content-Length and chunked responses; we don't know the
      // total size up front for chunked, so guard each append against the cap.
      if (buffer == NULL) {
        buffer = pvPortMalloc(MAX_CONFIG_RESPONSE_SIZE);
        if (buffer == NULL) {
          ESP_LOGE(FETCH_CONFIG_TASK, "failed to allocate response buffer");
          return ESP_ERR_NO_MEM;
        }
      }

      if (bytes_written + evt->data_len > MAX_CONFIG_RESPONSE_SIZE) {
        ESP_LOGE(FETCH_CONFIG_TASK, "response too large (> %d bytes)", MAX_CONFIG_RESPONSE_SIZE);
        return ESP_ERR_INVALID_SIZE;
      }
      memcpy(buffer + bytes_written, evt->data, evt->data_len);
      bytes_written += evt->data_len;
      break;

    default:
      break;
  }
  return ESP_OK;
}

static void send_http_request() {
  if (!(xEventGroupGetBits(event_group) & WIFI_CONNECTED)) {
    ESP_LOGI(FETCH_CONFIG_TASK, "WiFi not connected, skipping config fetch");
    return;
  }

  ESP_LOGI(FETCH_CONFIG_TASK, "start fetching config, etag = %ld", all_lists_checksum);

  esp_http_client_config_t config = {
      .host = API_HOST,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .path = "/api/kultcash/lists",
      .event_handler = _http_event_handler,
      .timeout_ms = 30000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  http_auth_headers(client);
  char etag[14];
  sprintf(etag, "\"%ld\"", all_lists_checksum);
  esp_http_client_set_header(client, "If-None-Match", etag);

  xSemaphoreTake(network_request, portMAX_DELAY);
  esp_err_t err = esp_http_client_perform(client);
  xSemaphoreGive(network_request);

  if (err != ESP_OK) {
    ESP_LOGE(FETCH_CONFIG_TASK, "HTTP request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    vPortFree(buffer);
    buffer = NULL;
    return;
  }

  int status_code = esp_http_client_get_status_code(client);
  ESP_LOGI(
      FETCH_CONFIG_TASK,
      "HTTP GET Status = %d, content_length = %" PRId64,
      status_code,
      esp_http_client_get_content_length(client)
  );
  esp_http_client_cleanup(client);

  switch (status_code) {
    case 200:
      AllLists all_lists = AllLists_init_default;
      pb_istream_t stream = pb_istream_from_buffer(buffer, bytes_written);

      // check if we can decode the protobuf
      if (!pb_decode(&stream, AllLists_fields, &all_lists)) {
        ESP_LOGE(FETCH_CONFIG_TASK, "failed to decode protobuf");
        break;
      }
      pb_release(AllLists_fields, &all_lists);

      FILE* config_file = fopen(CONFIG_FILE, "w");
      size_t files_written = fwrite(buffer, bytes_written, 1, config_file);
      ESP_LOGI(FETCH_CONFIG_TASK, "written %d files", files_written);
      fclose(config_file);
      xEventGroupSetBits(event_group, CONFIG_UPDATE_PENDING);
      int new_list_id = -1;
      xQueueSend(config_update_queue, &new_list_id, 0);
      break;
    default:
      ESP_LOGI(FETCH_CONFIG_TASK, "HTTP status code %d", status_code);
      break;
  }

  vPortFree(buffer);
  buffer = NULL;
}

static void periodic_config_timer_callback(TimerHandle_t xTimer) {
  xTaskNotifyGive(xTaskGetHandle(FETCH_CONFIG_TASK));
}

void fetch_config(void* params) {
  xEventGroupWaitBits(
      event_group, (READY_TO_FETCH_CONFIG | WIFI_CONNECTED), pdFALSE, pdTRUE, portMAX_DELAY
  );

  // Create and start periodic timer after initial fetch
  periodic_config_timer = xTimerCreate(
      "config_timer",
      pdMS_TO_TICKS(CONFIG_CHECK_INTERVAL_MS),
      pdTRUE,
      0,
      periodic_config_timer_callback
  );
  xTimerStart(periodic_config_timer, 0);

  while (true) {
    send_http_request();
    xEventGroupSetBits(event_group, INITIAL_FETCH_DONE);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
}
