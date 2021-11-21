#include <AceRoutine.h>
#include <Arduino.h>
#include <ArduinoLog.h>
#include <ESP8266WiFi.h>

using namespace ace_routine;

class SoftwareUpdateCoroutine : public Coroutine {
 public:
  int runCoroutine() override;
};