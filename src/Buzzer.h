#pragma once
#include <Arduino.h>

class Buzzer {
  int pin;
  unsigned long beepUntil;

 public:
  Buzzer(int address);
  void beep(int time);
  void setup();
  void loop();
};
