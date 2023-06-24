#include "InfoCoroutine.h"

#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "MainCoroutine.h"
#include "WiFiCoroutine.h"

extern const char* WIFI_SSID;
extern const int BUILD_NUMBER;
extern char deviceToken[48];

extern MainCoroutine mainCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern DisplayCoroutine displayCoroutine;
extern WiFiCoroutine wiFiCoroutine;
extern LogCoroutine logCoroutine;
extern char deviceID[9];

WiFiClient client;

void update_started() {
  Log.infoln("[SoftwareUpdate] HTTP update process started");
  mainCoroutine.mode = SOFTWARE_UPDATE;
  displayCoroutine.show("Software-Update", "verfügbar");
}

void update_finished() {
  Log.infoln("[SoftwareUpdate] HTTP update process finished");
  displayCoroutine.show("Software-Update", "erfolgreich");
}

void update_progress(int cur, int total) {
  Log.infoln("[SoftwareUpdate] HTTP update process at %d of %d bytes...\n", cur,
             total);
  char progress[17];
  sprintf(progress, "Download: %*i%%", 3, 100 * cur / total);
  displayCoroutine.show("Software-Update", progress);
}

void update_error(int err) {
  Log.infoln("[SoftwareUpdate] HTTP update fatal error code %d\n", err);
  char error[17];
  sprintf(error, "Fehler: %d", err);
  displayCoroutine.show("Software-Update", error, -2000);
}

int InfoCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT(mainCoroutine.mode == DEBUG_INFO);

    snprintf(line1, 17, "Geräte ID");
    snprintf(line2, 17, "%s", deviceID);
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    if (strlen(configCoroutine.config.name) > 0) {
      snprintf(line1, 17, configCoroutine.config.name);
      snprintf(line2, 17, "%d Produkt%c", configCoroutine.config.products_count,
               configCoroutine.config.products_count == 1 ? ' ' : 'e');
    } else {
      snprintf(line1, 17, "Keine Preisliste");
      snprintf(line2, 17, "vorhanden");
    }
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);
    snprintf(line1, 17, "Transaktionen");
    snprintf(line2, 17, "zum Upload: %d", logCoroutine.filesToUpload);
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    snprintf(line1, 17, "WLAN: %s", WIFI_SSID);
    snprintf(line2, 17, "%s",
             WiFi.status() == WL_CONNECTED ? "verbunden" : "nicht verbunden");
    displayCoroutine.requiresUpdate = true;

    COROUTINE_DELAY(2000);

    char build[] = "debug";
    bool canCheckForUpdate = false;
    if (BUILD_NUMBER > 0) {
      itoa(BUILD_NUMBER, build, 10);
      if (WiFi.status() == WL_CONNECTED) {
        canCheckForUpdate = true;
      }
    }
    snprintf(line1, 17, "Version: %s", build);
    snprintf(line2, 17, canCheckForUpdate ? "(# für Update)" : "");
    displayCoroutine.requiresUpdate = true;

    time = millis();

    COROUTINE_AWAIT(keypadCoroutine.currentKey == '#' ||
                    millis() - time > 3000);
    if (keypadCoroutine.currentKey == '#') {
      Log.infoln("[SoftwareUpdate] Check for updates");
      snprintf(line1, 17, "Software-Update");
      snprintf(line2, 17, "suchen...");
      displayCoroutine.requiresUpdate = true;
      COROUTINE_YIELD();

      ESPhttpUpdate.setAuthorization(deviceToken);
      ESPhttpUpdate.rebootOnUpdate(true);
      ESPhttpUpdate.onStart(update_started);
      ESPhttpUpdate.onProgress(update_progress);
      ESPhttpUpdate.onError(update_error);
      ESPhttpUpdate.onEnd(update_finished);

      t_httpUpdate_return ret =
          ESPhttpUpdate.update(client, "api.kulturspektakel.de", 51180,
                               "/$$$/update", String(BUILD_NUMBER));

      switch (ret) {
        case HTTP_UPDATE_FAILED:
          snprintf(line1, 17, "Software-Update");
          snprintf(line2, 17, "fehlgeschlagen");
          Log.infoln("[SoftwareUpdate] Update failed: %d %s",
                     ESPhttpUpdate.getLastError(),
                     ESPhttpUpdate.getLastErrorString());
          break;
        case HTTP_UPDATE_NO_UPDATES:
          snprintf(line1, 17, "Software-Update");
          snprintf(line2, 17, "kein Update");
          Log.infoln("[SoftwareUpdate] No update available.");
          break;
        case HTTP_UPDATE_OK:
          snprintf(line1, 17, "Software-Update");
          snprintf(line2, 17, "verfügbar");
          Log.infoln("[SoftwareUpdate] Update ok.");
          break;
      }
      displayCoroutine.requiresUpdate = true;
      COROUTINE_DELAY(2000);
    }

    mainCoroutine.defaultMode();
  }
}
