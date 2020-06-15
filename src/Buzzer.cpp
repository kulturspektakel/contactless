#include "Buzzer.h"

Buzzer::Buzzer(int p) {
  pin = p;
}

void Buzzer::setup() {
  pinMode(pin, OUTPUT);
}

void Buzzer::loop() {
  if (millis() < beepUntil) {
    digitalWrite(pin, HIGH);
  } else {
    digitalWrite(pin, LOW);
  }
}

void Buzzer::beep(int time) {
  beepUntil = millis() + time;
}
