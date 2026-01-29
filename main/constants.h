#pragma once

#define NVS_DEVICE_CONFIG "device_config"
#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASSWORD "wifi_password"
#define NVS_PRODUCT_LIST "product_list"
#define NVS_SALT "salt"
#define NVS_SOUND_MODE "sound_mode"

#define TZ "CET-1CEST,M3.5.0,M10.5.0/3"
#define API_HOST "api.kulturspektakel.de"
#define DEPOSIT_VALUE 200
#define MAX_BALANCE 9999
#define MAX_DEPOSIT 9

#define TASK_PRIO_NORMAL 5
#define TASK_PRIO_HIGH 10

#define FETCH_CONFIG_TASK "fetch_config"
#define LOG_UPLOADER_TASK "log_uploader"
#define WIFI_CONNECT_TASK "wifi_connect"
#define POWER_MANAGEMENT_TASK "power_mgmt"
#define RFID_TASK "rfid"
#define BATTERY_TEST_TASK "battery_test"
#define BLUETOOTH_TASK "bluetooth"

extern const char* LOG_DIR;
extern const char* CONFIG_FILE;

#define SALT_LENGTH 32
#define DEVICE_ID_LENGTH 16
extern const char SALT[SALT_LENGTH + 1];
extern const char DEVICE_ID[DEVICE_ID_LENGTH + 1];

void load_device_id(void* params);
void load_salt(void* params);