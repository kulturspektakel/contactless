#include "esp_mac.h"

void device_id(char* dest) {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  sprintf(dest, "%02X:%02X:%02X", mac[3], mac[4], mac[5]);
}