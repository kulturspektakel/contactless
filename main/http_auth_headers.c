
#include "http_auth_headers.h"
#include "constants.h"
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

void http_auth_headers(esp_http_client_handle_t client) {
  // TODO: version numbering
  esp_http_client_set_header(client, "User-Agent", "Contactless/3.2");
  char hash_input[DEVICE_ID_LENGTH + SALT_LENGTH + 1];

  esp_http_client_set_authtype(client, HTTP_AUTH_TYPE_BASIC);

  sprintf(hash_input, "%s%s", DEVICE_ID, SALT);
  uint8_t hash[20];
  create_sha1_hash(hash_input, strlen(hash_input), hash);
  char authorization[48];
  // TODO base64 encode
  snprintf(authorization, sizeof(authorization), "Basic %s:%s", DEVICE_ID, hash);
  esp_http_client_set_header(client, "Authorization", authorization);
}