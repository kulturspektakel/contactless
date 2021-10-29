#include "WiFiCoroutine.h"
#include <ArduinoLog.h>
#include <ESP8266WiFi.h>

extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

int WiFiCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    Log.infoln("[WiFi] Connect to %s (%s)", WIFI_SSID, WIFI_PASSWORD);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED ||
                    WiFi.status() == WL_CONNECT_FAILED);
    if (WiFi.status() == WL_CONNECTED) {
      Log.infoln("[WiFi] Connected");
    }
    COROUTINE_AWAIT(WiFi.status() != WL_CONNECTED);
    Log.infoln("[WiFi] Disconnected");
    COROUTINE_DELAY_SECONDS(2 * 60);
  }
}
