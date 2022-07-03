#include "SoftwareUpdateCoroutine.h"
#include <ArduinoLog.h>
#include <ESP8266httpUpdate.h>
#include "DisplayCoroutine.h"
#include "MainCoroutine.h"

extern char deviceToken[48];
extern const int BUILD_NUMBER;
extern DisplayCoroutine displayCoroutine;
extern MainCoroutine mainCoroutine;

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
  displayCoroutine.show("Software-Update", error);
}

int SoftwareUpdateCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED);
  COROUTINE_DELAY_SECONDS(7);  // wait for config

  Log.infoln("[SoftwareUpdate] Check for updates");
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
      Log.infoln("[SoftwareUpdate] Update failed: %d %s",
                 ESPhttpUpdate.getLastError(),
                 ESPhttpUpdate.getLastErrorString());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Log.infoln("[SoftwareUpdate] No update available.");
      break;
    case HTTP_UPDATE_OK:
      Log.infoln("[SoftwareUpdate] Update ok.");
      break;
  }

  COROUTINE_END();
}
