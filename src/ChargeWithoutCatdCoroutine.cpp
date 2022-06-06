#include "ChargeWithoutCatdCoroutine.h"

#include <ArduinoLog.h>
#include "DisplayCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "MainCoroutine.h"

extern MainCoroutine mainCoroutine;
extern DisplayCoroutine displayCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern LogCoroutine logCoroutine;
extern const char* MODE_CHANGER;

int ChargeWithoutCatdCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT((mainCoroutine.mode == CHARGE_MANUAL ||
                     mainCoroutine.mode == CHARGE_LIST) &&
                    keypadCoroutine.currentKey == '*' &&
                    mainCoroutine.balance != 0);
    starPress = millis();
    displayCoroutine.show("1) Crew  2) Band", "3) Gutschein", 3000);
    COROUTINE_YIELD();
    COROUTINE_AWAIT(keypadCoroutine.currentKey != '\0');

    if (millis() < starPress + 2500) {
      CardTransaction_PaymentMethod p = CardTransaction_PaymentMethod_KULT_CARD;
      if (keypadCoroutine.currentKey == '1') {
        p = CardTransaction_PaymentMethod_FREE_CREW;
      } else if (keypadCoroutine.currentKey == '2') {
        p = CardTransaction_PaymentMethod_FREE_CREW;
      } else if (keypadCoroutine.currentKey == '3') {
        p = CardTransaction_PaymentMethod_FREE_CREW;
      }

      if (p != CardTransaction_PaymentMethod_KULT_CARD) {
        logCoroutine.writeLog(p);
        mainCoroutine.resetBalance();

      } else {
        displayCoroutine.clearMessageIn(0);
      }
    }

    if (keypadCoroutine.currentKey == '1' ||
        keypadCoroutine.currentKey == '2' ||
        keypadCoroutine.currentKey == '3') {
    } else {
      displayCoroutine.clearMessageIn(0);
    }
  }
}