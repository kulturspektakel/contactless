
#include "http_auth_headers.h"
#include "device_id.h"
#include "esp_mac.h"
#include "mbedtls/sha1.h"
#include "nvs_flash.h"

void create_sha1_hash(const char* input_data, size_t input_length, uint8_t* output_hash) {
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, (const unsigned char*)input_data, input_length);
  mbedtls_sha1_finish(&ctx, (unsigned char*)output_hash);
  mbedtls_sha1_free(&ctx);
}

char* alloc_slat() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open("device_config", NVS_READONLY, &nvs_handle));
  size_t salt_size;
  nvs_get_str(nvs_handle, "salt", NULL, &salt_size);
  char* salt = pvPortMalloc(salt_size);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "salt", salt, &salt_size));
  nvs_close(nvs_handle);
  return salt;
}

void http_auth_headers(esp_http_client_handle_t client) {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  char mac_str[18];
  sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  esp_http_client_set_header(client, "x-ESP8266-STA-MAC", mac_str);
  char hash_input[41];

  char* salt = alloc_slat();
  char did[9];
  device_id(did);
  sprintf(hash_input, "%s%s", did, salt);
  vPortFree(salt);
  uint8_t hash[20];
  create_sha1_hash(hash_input, strlen(hash_input), hash);
  char authorization[48] = "Bearer ";
  for (int i = 0; i < 20; i++) {
    sprintf(authorization + 7 + (i * 2), "%02x", hash[i]);
  }
  esp_http_client_set_header(client, "Authorization", authorization);
}