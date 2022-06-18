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
    COROUTINE_AWAIT(mainCoroutine.mode == CHARGE_WITHOUT_CARD);
    starPress = millis();
    COROUTINE_AWAIT(keypadCoroutine.currentKey != '\0');

    if (keypadCoroutine.currentKey == '1' ||
        keypadCoroutine.currentKey == '2' ||
        keypadCoroutine.currentKey == '3') {
      LogMessage_Order_PaymentMethod p =
          LogMessage_Order_PaymentMethod_KULT_CARD;
      if (keypadCoroutine.currentKey == '1') {
        p = LogMessage_Order_PaymentMethod_FREE_CREW;
        displayCoroutine.show("Crew", "abgerechnet", 1500);
      } else if (keypadCoroutine.currentKey == '2') {
        p = LogMessage_Order_PaymentMethod_CASH;
        displayCoroutine.show("Barzahlung", "abgerechnet", 1500);
      } else if (keypadCoroutine.currentKey == '3') {
        p = LogMessage_Order_PaymentMethod_VOUCHER;
        displayCoroutine.show("Gutschein", "abgerechnet", 1500);
      }
      logCoroutine.writeLog(p);
      mainCoroutine.resetBalance();
    }

    mainCoroutine.defaultMode();
  }
}