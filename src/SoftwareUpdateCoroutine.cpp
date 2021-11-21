#include "SoftwareUpdateCoroutine.h"
#include <ArduinoLog.h>
#include <ESP8266httpUpdate.h>

WiFiClient client;
int SoftwareUpdateCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED);

  Log.infoln("[SoftwareUpdate] Check for updates");
  t_httpUpdate_return ret = ESPhttpUpdate.update(
      client, "api.kulturspektakel.de", 51180, "/$$$/update",
      "optional current version string here");
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Log.infoln("[SoftwareUpdate] Update failed.");
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Log.infoln("[SoftwareUpdate] Update no update.");
      break;
    case HTTP_UPDATE_OK:
      Log.infoln("[SoftwareUpdate] Update ok.");
      break;
  }

  COROUTINE_END();
}
