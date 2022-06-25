#include "BuzzerCoroutine.h"

#include <ArduinoLog.h>

const uint8_t BUZZER_PIN = 0;

int BuzzerCoroutine::runCoroutine() {
  pinMode(BUZZER_PIN, OUTPUT);
  COROUTINE_LOOP() {
    // turn off
    lastTime = 0;
    beepCounter = 0;
    digitalWrite(BUZZER_PIN, LOW);
    COROUTINE_AWAIT(beepCounter > 0);

    while (beepCounter > 0) {
      if (lastTime == 0) {
        lastTime = millis();
      } else {
        beepCounter -= millis() - lastTime;
        lastTime = millis();
      }
      digitalWrite(BUZZER_PIN, HIGH);
      COROUTINE_YIELD();
    }
  }
}

void BuzzerCoroutine::beep(BeepPattern pattern) {
  switch (pattern) {
    case BEEP:
      beepCounter += 250;
      break;
    case ERROR:
      beepCounter += 1000;
      break;
  }
}
