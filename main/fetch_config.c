#include "fetch_config.h"
#include "configs.pb.h"
#include "constants.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "event_group.h"
#include "http_auth_headers.h"
#include "local_config.h"
#include "pb_decode.h"

static const char* TAG = "fetch_config";

static size_t bytes_written = 0;
static uint8_t* buffer = NULL;

esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      bytes_written = 0;
      break;

    case HTTP_EVENT_ON_DATA:
      if (esp_http_client_is_chunked_response(evt->client)) {
        ESP_LOGE(TAG, "chunked response is not supported");
        return ESP_ERR_NOT_SUPPORTED;
      }

      if (esp_http_client_get_content_length(evt->client) > 4096) {
        ESP_LOGE(TAG, "content length too large");
        return ESP_ERR_INVALID_SIZE;
      }

      if (bytes_written == 0) {
        buffer = pvPortMalloc(esp_http_client_get_content_length(evt->client));
      }
      memcpy(buffer + bytes_written, evt->data, evt->data_len);
      bytes_written += evt->data_len;
      break;

    default:
      break;
  }
  return ESP_OK;
}

void fetch_config(void* params) {
  xEventGroupWaitBits(
      event_group, (LOCAL_CONFIG_LOADED | WIFI_CONNECTED), pdFALSE, pdTRUE, portMAX_DELAY
  );

  ESP_LOGI(TAG, "start fetching config");

  esp_http_client_config_t config = {
      .host = API_HOST,
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .path = "/$$$/lists",
      .event_handler = _http_event_handler,
      .timeout_ms = 30000,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  http_auth_headers(client);
  char etag[14];
  sprintf(etag, "\"%ld\"", all_lists_checksum);
  esp_http_client_set_header(client, "If-None-Match", etag);
  esp_err_t err = esp_http_client_perform(client);

  if (err != ESP_OK) {
    // TODO: maybe retry? (e.g. timeout)
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return vTaskDelete(NULL);
  }

  int status_code = esp_http_client_get_status_code(client);
  ESP_LOGI(
      TAG,
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
        ESP_LOGE(TAG, "failed to decode protobuf");
        break;
      }
      pb_release(AllLists_fields, &all_lists);

      FILE* config_file = fopen(CONFIG_FILE, "w");
      size_t files_written = fwrite(buffer, bytes_written, 1, config_file);
      ESP_LOGI(TAG, "written %d files", files_written);
      fclose(config_file);
      xEventGroupSetBits(event_group, LOCAL_CONFIG_UPDATED);
      break;
    default:
      ESP_LOGI(TAG, "HTTP status code %d", status_code);
      break;
  }

  vPortFree(buffer);
  xEventGroupSetBits(event_group, REMOTE_CONFIG_FETCHED);
  vTaskDelete(NULL);
}
