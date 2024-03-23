#pragma once
#include "esp_http_client.h"

void http_auth_headers(esp_http_client_handle_t client);
void create_sha1_hash(const char* input_data, size_t input_length, uint8_t* output_hash);
