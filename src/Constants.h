#include "MFRC522.h"

extern const int YEAR = 2021;
extern const int TOKEN_VALUE = 200;
extern const int TIMEZONE_OFFSET_MINUTES = 60;

// populated from environment variables
extern const char* MODE_CHANGER = ENV_MODE_CHANGER;
extern const char* SALT = ENV_SALT;
extern MFRC522::MIFARE_Key KEY_B = {{ENV_KEY_B}};
extern const char* WIFI_SSID = ENV_WIFI_SSID;
extern const char* WIFI_PASSWORD = ENV_WIFI_PASSWORD;
