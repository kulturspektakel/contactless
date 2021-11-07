#define SPI_DRIVER_SELECT 1
#include <AceRoutine.h>
#include <Arduino.h>
#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <SD.h>
#include <SPI.h>
#include "ChargeManualCoroutine.h"
#include "ConfigCoroutine.h"
#include "Constants.h"
#include "DisplayCoroutine.h"
#include "InfoCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "MainCoroutine.h"
#include "ModeChangerCoroutine.h"
#include "RFIDCoroutine.h"
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

char deviceID[9];
char deviceToken[48];

void setup() {
  snprintf(deviceID, 9, WiFi.macAddress().substring(9).c_str());
  snprintf(deviceToken, 48, "Bearer %s", sha1(String(deviceID) + SALT).c_str());

  Serial.begin(9600);
  Log.begin(LOG_LEVEL_VERBOSE, &Serial);
  SPI.begin();

  // disable RFID
  // pinMode(15, OUTPUT);
  // digitalWrite(15, HIGH);
  if (!SD.begin(10)) {
    Log.errorln("No SD card");
  }
}

void loop() {
  keypadCoroutine.runCoroutine();
  wiFiCoroutine.runCoroutine();
  displayCoroutine.runCoroutine();
  configCoroutine.runCoroutine();  // after display
  mainCoroutine.runCoroutine();
  timeEntryCoroutine.runCoroutine();
  logCoroutine.runCoroutine();
  chargeManualCoroutine.runCoroutine();
  rFIDCoroutine.runCoroutine();
  infoCoroutine.runCoroutine();
  modeChangerCoroutine.runCoroutine();
}
