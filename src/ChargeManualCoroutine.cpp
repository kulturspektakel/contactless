#include "ChargeManualCoroutine.h"

#include <ArduinoLog.h>
#include <ConfigCoroutine.h>
#include <DisplayCoroutine.h>
#include <KeypadCoroutine.h>
#include <MainCoroutine.h>

extern MainCoroutine mainCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern DisplayCoroutine displayCoroutine;

int ChargeManualCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT((mainCoroutine.mode == CHARGE_MANUAL ||
                     mainCoroutine.mode == CHARGE_LIST) &&
                    keypadCoroutine.currentKey != '\0');

    if (keypadCoroutine.currentKey == '#' &&
        (lastKey == 0 || (millis() - lastKey) < 700)) {
      lastKey = millis();
      if (++count > 2) {
        mainCoroutine.mode =
            mainCoroutine.mode == CHARGE_MANUAL ? CHARGE_LIST : CHARGE_MANUAL;
        mainCoroutine.resetBalance();
        count = 0;
      }
    } else {
      if (keypadCoroutine.currentKey == 'D' &&
          mainCoroutine.mode == CHARGE_MANUAL &&
          configCoroutine.config.products_count > 0) {
        // revert back to list mode
        mainCoroutine.mode = CHARGE_LIST;
        mainCoroutine.resetBalance();
      }
      count = 0;
      lastKey = 0;
    }
  }
}
