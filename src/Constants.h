#include "MFRC522.h"

#define STR(x...) #x
#define XSTR(x) STR(x)

extern const int YEAR = EYEAR;
extern const int TOKEN_VALUE = ETOKEN_VALUE;
extern const std::vector<char*> MODE_CHANGER{EMODE_CHANGER};
extern const char* SALT = XSTR(ESALT);
extern MFRC522::MIFARE_Key KEY_B = {{EKEY_B}};
extern const char* WIFI_SSID = XSTR(EWIFI_SSID);
extern const char* WIFI_PASSWORD = XSTR(EWIFI_PASSWORD);
extern const int TIMEZONE_OFFSET_MINUTES = ETIMEZONE_OFFSET_MINUTES;
