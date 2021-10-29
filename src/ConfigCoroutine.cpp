#include "ConfigCoroutine.h"
#include <ArduinoLog.h>
#include <TimeLib.h>
#include <pb_decode.h>
#include "DisplayCoroutine.h"
#include "MainCoroutine.h"
#include "SdFat.h"
#include "TimeEntryCoroutine.h"

using namespace sdfat;

extern TimeEntryCoroutine timeEntryCoroutine;
extern MainCoroutine mainCoroutine;
extern DisplayCoroutine displayCoroutine;
extern SdFat sd;
extern char deviceID[9];
extern char deviceToken[48];

static asyncHTTPrequest request;
static const char* configFileName = "_config.cfg";

int ConfigCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  sdfat::File localFile = sd.open(configFileName, O_RDONLY);

  if (localFile && localFile.isOpen()) {
    size_t len = localFile.size();
    uint8_t data[len];
    localFile.readBytes(data, len);
    localFile.close();

    pb_istream_t stream = pb_istream_from_buffer(data, len);
    pb_decode(&stream, DeviceConfig_fields, &config);
    Log.infoln("[Config] loaded: %s", config.name);
    displayCoroutine.show(deviceID, config.name, -1, -1, 2000);
  } else {
    Log.infoln("[Config] no stored config");
  }

  COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED);

  request.open("GET", "http://api.kulturspektakel.de:51180/$$$/config");
  request.setReqHeader("x-ESP8266-STA-MAC", deviceID);
  request.setReqHeader("Authorization", deviceToken);
  request.send();

  COROUTINE_AWAIT(request.readyState() == 4);

  Log.infoln("[Config] Received response HTTP %d", request.responseHTTPcode());
  timeEntryCoroutine.dateFromHTTP(request.respHeaderValue("Date"));

  if (request.responseHTTPcode() == 200) {
    // config available
    size_t len = request.responseLength();
    uint8_t buffer[len];
    request.responseRead(buffer, len);
    pb_istream_t pbstream = pb_istream_from_buffer(buffer, len);

    pb_decode(&pbstream, DeviceConfig_fields, &config);
    Log.infoln("[Config] received: %s", config.name);

    // write to file
    sdfat::File file = sd.open(configFileName, sdfat::O_RDWR | sdfat::O_CREAT);
    int written = file.write(buffer, len);
    Log.infoln("[Config] Written: %d", written);
    file.close();
  } else if (request.responseHTTPcode() == 204) {
    // delete config
    sd.remove(configFileName);
    Log.infoln("[Config] No config. Deleting file.");
  }

  if (mainCoroutine.mode != TIME_ENTRY) {
    mainCoroutine.mode =
        config.products_count > 0 ? CHARGE_LIST : CHARGE_MANUAL;
  }

  COROUTINE_END();
}
