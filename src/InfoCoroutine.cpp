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

extern MainCoroutine mainCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern DisplayCoroutine displayCoroutine;
extern WiFiCoroutine wiFiCoroutine;
extern LogCoroutine logCoroutine;
extern char deviceID[9];

int InfoCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT(mainCoroutine.mode != TIME_ENTRY &&
                    keypadCoroutine.currentKey != '\0');

    if (keypadCoroutine.currentKey == '*') {
      lastKey = millis();
    } else if (keypadCoroutine.currentKey == '#' && lastKey > 0 &&
               lastKey < millis() + 500) {
      lastKey = 0;
      // TODO try connecting to WiFi
      if (WiFi.status() != WL_CONNECTED) {
        wiFiCoroutine.reset();
      }
      char line1[17];

      displayCoroutine.show("Geräte ID", deviceID);
      COROUTINE_DELAY(2000);

      if (strlen(configCoroutine.config.name) > 0) {
        snprintf(line1, 16, "%d Produkt%c",
                 configCoroutine.config.products_count,
                 configCoroutine.config.products_count == 1 ? ' ' : 'e');
        displayCoroutine.show(configCoroutine.config.name, line1);
      } else {
        displayCoroutine.show("Keine Preisliste", "vorhanden");
      }
      COROUTINE_DELAY(2000);

      snprintf(line1, 16, "%d Transaktion%s", logCoroutine.logsToUpload,
               logCoroutine.logsToUpload == 1 ? "" : "en");
      displayCoroutine.show(line1, "zum Upload");
      COROUTINE_DELAY(2000);

      // Screen 2: WiFi + Logs to upload
      snprintf(line1, 16, "WLAN: %s", WIFI_SSID);
      displayCoroutine.show(line1, WiFi.status() == WL_CONNECTED
                                       ? "verbunden"
                                       : "nicht verbunden");
      COROUTINE_DELAY(2000);

      // Screen 3: Software version
      displayCoroutine.show("Software", "1.0.0", -1, -1, 2000);
    } else {
      lastKey = 0;
    }
  }
}
