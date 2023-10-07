#include "fetch_config.h"
#include "configs.pb.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "event_group.h"
#include "local_config.h"
#include "mbedtls/sha1.h"
#include "nvs_flash.h"
#include "pb_decode.h"

static const char* TAG = "fetch_config";

static size_t bytes_written = 0;
static uint8_t* buffer = NULL;

void create_sha1_hash(const char* input_data, size_t input_length, uint8_t* output_hash) {
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, (const unsigned char*)input_data, input_length);
  mbedtls_sha1_finish(&ctx, (unsigned char*)output_hash);
  mbedtls_sha1_free(&ctx);
}

void add_auth_headers(esp_http_client_handle_t client) {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char mac_str[18];
  sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  esp_http_client_set_header(client, "x-ESP8266-STA-MAC", mac_str);
  char hash_input[41];

  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open("device_config", NVS_READONLY, &nvs_handle));
  size_t salt_size;
  nvs_get_str(nvs_handle, "salt", NULL, &salt_size);
  char* salt = malloc(salt_size);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "salt", salt, &salt_size));
  nvs_close(nvs_handle);
  sprintf(hash_input, "%02X:%02X:%02X%s", mac[3], mac[4], mac[5], salt);
  free(salt);
  uint8_t hash[20];
  create_sha1_hash(hash_input, strlen(hash_input), hash);
  char authorization[48] = "Bearer ";
  for (int i = 0; i < 20; i++) {
    sprintf(authorization + 7 + (i * 2), "%02x", hash[i]);
  }
  esp_http_client_set_header(client, "Authorization", authorization);
}

esp_err_t _http_event_handler(esp_http_client_event_t* evt) {
  switch (evt->event_id) {
    case HTTP_EVENT_ON_CONNECTED:
      bytes_written = 0;
      add_auth_headers(evt->client);
      char etag[14];
      sprintf(etag, "\"%ld\"", active_config.checksum);
      esp_http_client_set_header(evt->client, "If-None-Match", etag);
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
        buffer = malloc(esp_http_client_get_content_length(evt->client));
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
      .host = "api.kulturspektakel.de",
      .transport_type = HTTP_TRANSPORT_OVER_SSL,
      .path = "/$$$/lists",
      .event_handler = _http_event_handler,
  };

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_perform(client);

  if (err != ESP_OK) {
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

      FILE* config_file = fopen("/littlefs/config.cfg", "w");
      size_t files_written = fwrite(buffer, bytes_written, 1, config_file);
      ESP_LOGI(TAG, "written %d files", files_written);
      fclose(config_file);
      xEventGroupSetBits(event_group, LOCAL_CONFIG_UPDATED);
      break;
    default:
      ESP_LOGI(TAG, "HTTP status code %d", status_code);
      break;
  }

  free(buffer);
  xEventGroupSetBits(event_group, REMOTE_CONFIG_FETCHED);
  vTaskDelete(NULL);
}
