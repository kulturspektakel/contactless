#include "SoftwareUpdateCoroutine.h"
#include <ArduinoLog.h>
#include <ESP8266httpUpdate.h>

extern char deviceToken[48];
extern const int BUILD_NUMBER;
WiFiClient client;

int SoftwareUpdateCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  // if (BUILD_NUMBER == 0) {
  //   // TODO: not updating debug builds
  //   COROUTINE_END();
  // }

  COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED);

  Log.infoln("[SoftwareUpdate] Check for updates");
  ESPhttpUpdate.setAuthorization(deviceToken);

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
