#include "InfoCoroutine.h"

#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "MainCoroutine.h"
#include "WiFiCoroutine.h"

extern const char* WIFI_SSID;
extern const int BUILD_NUMBER;

extern MainCoroutine mainCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern DisplayCoroutine displayCoroutine;
extern WiFiCoroutine wiFiCoroutine;
extern LogCoroutine logCoroutine;
extern char deviceID[9];

int InfoCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT(mainCoroutine.mode == DEBUG_INFO);

    snprintf(line1, 16, "Geräte ID");
    snprintf(line2, 16, "%s", deviceID);
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    if (strlen(configCoroutine.config.name) > 0) {
      snprintf(line1, 16, configCoroutine.config.name);
      snprintf(line2, 16, "%d Produkt%c", configCoroutine.config.products_count,
               configCoroutine.config.products_count == 1 ? ' ' : 'e');
    } else {
      snprintf(line1, 16, "Keine Preisliste");
      snprintf(line2, 16, "vorhanden");
    }
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    snprintf(line1, 16, "%d Transaktion%s", logCoroutine.logsToUpload,
             logCoroutine.logsToUpload == 1 ? "" : "en");
    snprintf(line2, 16, "zum Upload");
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    snprintf(line1, 16, "WLAN: %s", WIFI_SSID);
    snprintf(line2, 16, "%s",
             WiFi.status() == WL_CONNECTED ? "verbunden" : "nicht verbunden");
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    char build[] = "debug";
    if (BUILD_NUMBER > 0) {
      itoa(BUILD_NUMBER, build, 10);
    }
    snprintf(line1, 16, "Software");
    snprintf(line2, 16, "%s", build);
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);
    mainCoroutine.defaultMode();
  }
}
