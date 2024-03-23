
#include "http_auth_headers.h"
#include <esp_app_desc.h>
#include "constants.h"
#include "esp_log.h"
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

void uint8_to_ascii(uint8_t* array, int length, char* result) {
  for (int i = 0; i < length; i++) {
    sprintf(&result[i * 2], "%02x", array[i]);
  }
  result[length * 2] = '\0';
}

void http_auth_headers(esp_http_client_handle_t client) {
  char user_agent[45];
  const esp_app_desc_t* app_desc = esp_app_get_description();
  sprintf(user_agent, "Contactless/%s", app_desc->version);
  esp_http_client_set_header(client, "User-Agent", user_agent);
  char hash_input[DEVICE_ID_LENGTH + SALT_LENGTH + 1];
  sprintf(hash_input, "%s%s", DEVICE_ID, SALT);
  uint8_t hash[20];
  create_sha1_hash(hash_input, strlen(hash_input), hash);
  char password[sizeof(hash) * 2 + 1];
  uint8_to_ascii(hash, sizeof(hash), password);
  esp_http_client_set_username(client, DEVICE_ID);
  esp_http_client_set_password(client, password);
  esp_http_client_set_authtype(client, HTTP_AUTH_TYPE_BASIC);
}