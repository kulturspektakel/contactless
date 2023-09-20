
#include "http_event_handler.h"
#include "esp_log.h"

static const char* TAG = "http_event_handler";

esp_err_t http_event_handler(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      esp_http_client_set_header(evt->client, "Authorization", "Daniel");
      break;
    case HTTP_EVENT_ON_DATA:
      if (esp_http_client_is_chunked_response(evt->client)) {
        ESP_LOGE(TAG, "chunked response is not supported");
        return ESP_ERR_NOT_SUPPORTED;
      }

      int content_length = esp_http_client_get_content_length(evt->client);
      esp_http_client_get_chunk_length ESP_LOGI(TAG, "data %i", evt->client->data_written_index);
      // if (content_length > sizeof(evt->data)) {
      //   ESP_LOGE(
      //       TAG,
      //       "response data too large: content_length=%d data_len=%d",
      //       content_length,
      //       evt->data_len
      //   );
      //   return ESP_ERR_INVALID_SIZE;
      // }
      // memcpy(evt->user_data, evt->data, evt->data_len);
      break;
    default:
      break;
  }
  return ESP_OK;
}