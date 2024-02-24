#pragma once

#define NVS_DEVICE_CONFIG "device_config"
#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASSWORD "wifi_password"
#define NVS_PRODUCT_LIST "product_list"
#define NVS_SALT "salt"

#define MAX_PRIVILEGE_TOKENS 30

#define TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#define API_HOST "api.kulturspektakel.de"
#define DEPOSIT_VALUE 200

extern const char* LOG_DIR;
extern const char* CONFIG_FILE;

#define SALT_LENGTH 32
#define DEVICE_ID_LENGTH 16
extern const char SALT[SALT_LENGTH + 1];
extern const char DEVICE_ID[DEVICE_ID_LENGTH + 1];

void load_device_id(void* params);
void load_salt(void* params);