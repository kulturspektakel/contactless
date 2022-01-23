#include "ConfigCoroutine.h"
#include <ArduinoLog.h>
#include <SD.h>
#include <TimeLib.h>
#include <pb_decode.h>
#include "DisplayCoroutine.h"
#include "MainCoroutine.h"
#include "RFIDCoroutine.h"
#include "TimeEntryCoroutine.h"

extern TimeEntryCoroutine timeEntryCoroutine;
extern MainCoroutine mainCoroutine;
extern DisplayCoroutine displayCoroutine;
extern RFIDCoroutine rFIDCoroutine;
extern char deviceID[9];
extern char deviceToken[48];

static asyncHTTPrequest request;
static const char* configFileName = "_config.cfg";

int ConfigCoroutine::runCoroutine() {
  COROUTINE_BEGIN();
  rFIDCoroutine.resetReader();  // needed to free SPI
  SD.begin(10);
  // SD.remove(configFileName);
  configFile = SD.open(configFileName, FILE_READ);

  COROUTINE_AWAIT(displayCoroutine.initialized);

  if (configFile && configFile.available()) {
    size_t len = configFile.size();
    uint8_t data[len];
    configFile.read(data, len);
    configFile.close();

    pb_istream_t stream = pb_istream_from_buffer(data, len);
    pb_decode(&stream, DeviceConfig_fields, &config);
    Log.infoln("[Config] loaded: %s", config.name);
    displayCoroutine.show(deviceID, config.name, -2000);
  } else {
    Log.infoln("[Config] no stored config");
    displayCoroutine.show(deviceID, nullptr, -2000);
  }

  COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED);

  request.open("GET", "http://api.kulturspektakel.de:51180/$$$/config");
  request.setReqHeader("x-ESP8266-STA-MAC", WiFi.macAddress().c_str());
  request.setReqHeader("Authorization", deviceToken);
  request.send();

  COROUTINE_AWAIT(request.readyState() == 4);

  Log.infoln("[Config] Received response HTTP %d", request.responseHTTPcode());
  if (request.responseHTTPcode() > 0) {
    timeEntryCoroutine.dateFromHTTP(request.respHeaderValue("Date"));
  }

  if (request.responseHTTPcode() == 200) {
    // config available
    size_t len = request.responseLength();
    uint8_t buffer[len];
    request.responseRead(buffer, len);
    pb_istream_t pbstream = pb_istream_from_buffer(buffer, len);
    pb_decode(&pbstream, DeviceConfig_fields, &config);
    Log.infoln("[Config] received: %s", config.name);

    rFIDCoroutine.resetReader();  // needed to free SPI
    configFile = SD.open(configFileName, FILE_WRITE);

    if (configFile.availableForWrite()) {
      int written = configFile.write(buffer, len);
      Log.infoln("[Config] Written: %d", written);
      configFile.close();
    } else {
      Log.infoln("[Config] config not writable");
    }
  } else if (request.responseHTTPcode() == 204) {
    // delete config
    rFIDCoroutine.resetReader();  // needed to free SPI
    SD.remove(configFileName);
    DeviceConfig emptyConfig = DeviceConfig_init_zero;
    config = emptyConfig;
    Log.infoln("[Config] No config. Deleting file.");
  }

  if (mainCoroutine.mode != TIME_ENTRY) {
    mainCoroutine.mode =
        config.products_count > 0 ? CHARGE_LIST : CHARGE_MANUAL;
    displayCoroutine.requiresUpdate = true;
  }

  COROUTINE_END();
}
