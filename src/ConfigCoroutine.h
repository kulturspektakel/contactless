#include <AceRoutine.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <asyncHTTPrequest.h>
#include "proto/config.pb.h"

using namespace ace_routine;

class ConfigCoroutine : public Coroutine {
 private:
  void parseDate(char result[9], char* date);

 public:
  int runCoroutine() override;
  DeviceConfig config = DeviceConfig_init_zero;
};