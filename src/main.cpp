// Need to set ENABLE_DEDICATED_SPI 0 in
// .platformio/packages/framework-arduinoespressif8266/libraries/ESP8266SdFat/src/SdFatConfig.h

#include <AceRoutine.h>
#include <Arduino.h>
#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <SPI.h>
#include "ChargeManualCoroutine.h"
#include "ChargeWithoutCatdCoroutine.h"
#include "ConfigCoroutine.h"
#include "Constants.h"
#include "DisplayCoroutine.h"
#include "InfoCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "MainCoroutine.h"
#include "ModeChangerCoroutine.h"
#include "ProductNumberCoroutine.h"
#include "RFIDCoroutine.h"
#include "SoftwareUpdateCoroutine.h"
#include "TimeEntryCoroutine.h"
#include "WiFiCoroutine.h"

WiFiCoroutine wiFiCoroutine;
ConfigCoroutine configCoroutine;
DisplayCoroutine displayCoroutine;
MainCoroutine mainCoroutine;
KeypadCoroutine keypadCoroutine;
TimeEntryCoroutine timeEntryCoroutine;
LogCoroutine logCoroutine;
ChargeManualCoroutine chargeManualCoroutine;
RFIDCoroutine rFIDCoroutine;
InfoCoroutine infoCoroutine;
ModeChangerCoroutine modeChangerCoroutine;
SoftwareUpdateCoroutine softwareUpdateCoroutine;
ChargeWithoutCatdCoroutine chargeWithoutCatdCoroutine;
ProductNumberCoroutine productNumberCoroutine;

char deviceID[9];
char deviceToken[48];

void setup() {
  snprintf(deviceID, 9, WiFi.macAddress().substring(9).c_str());
  snprintf(deviceToken, 48, "Bearer %s", sha1(String(deviceID) + SALT).c_str());

  randomSeed(ESP.getCycleCount());

#ifndef DISABLE_LOGGING
  Serial.begin(9600);
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
#endif

  SPI.begin();
}

void loop() {
  keypadCoroutine.runCoroutine();
  wiFiCoroutine.runCoroutine();
  displayCoroutine.runCoroutine();
  rFIDCoroutine.runCoroutine();
  configCoroutine.runCoroutine();
  mainCoroutine.runCoroutine();
  timeEntryCoroutine.runCoroutine();
  logCoroutine.runCoroutine();
  chargeManualCoroutine.runCoroutine();
  infoCoroutine.runCoroutine();
  modeChangerCoroutine.runCoroutine();
  softwareUpdateCoroutine.runCoroutine();
  chargeWithoutCatdCoroutine.runCoroutine();
  productNumberCoroutine.runCoroutine();
}
