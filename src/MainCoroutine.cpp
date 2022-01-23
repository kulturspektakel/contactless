#include "MainCoroutine.h"
#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include <TimeLib.h>
#include <pb_decode.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "proto/product.pb.h"
#include "proto/transaction.pb.h"

extern DisplayCoroutine displayCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern LogCoroutine logCoroutine;
extern bool string16(pb_istream_t* stream,
                     const pb_field_t* field,
                     void** buffer);

int MainCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  COROUTINE_AWAIT(timeStatus() == timeSet);
  mode =
      configCoroutine.config.products_count > 0 ? CHARGE_LIST : CHARGE_MANUAL;
  displayCoroutine.requiresUpdate = true;

  while (true) {
    COROUTINE_AWAIT(keypadCoroutine.currentKey);

    bool isDigit =
        keypadCoroutine.currentKey >= '0' && keypadCoroutine.currentKey <= '9';

    if (keypadCoroutine.currentKey == 'D') {
      // reset
      resetBalance();
    } else if (keypadCoroutine.currentKey == 'A' && balance.deposit < 9) {
      // increase token
      balance.deposit++;
    } else if (keypadCoroutine.currentKey == 'B' && balance.deposit > -9) {
      // reduce deposit
      balance.deposit--;
    } else if (keypadCoroutine.currentKey == 'C' &&
               (mode == TOP_UP || mode == CHARGE_MANUAL)) {
      balance.deposit /= 10;
    } else if (isDigit && balance.total < 1000 &&
               (mode == TOP_UP || mode == CHARGE_MANUAL)) {
      // enter amount
      balance.total *= 10;
      balance.total += keypadCoroutine.currentKey - '0';
    } else if (isDigit && mode == CHARGE_LIST &&
               keypadCoroutine.currentKey != '0' &&
               keypadCoroutine.currentKey - '1' <
                   configCoroutine.config.products_count &&
               balance.total + configCoroutine.config
                                   .products[keypadCoroutine.currentKey - '1']
                                   .price <
                   10000) {
      int index = keypadCoroutine.currentKey - '1';
      // add product
      balance.total += configCoroutine.config.products[index].price;
      logCoroutine.addProduct(index);
      displayCoroutine.show(configCoroutine.config.products[index].name,
                            nullptr, -2000,
                            configCoroutine.config.products[index].price);
    } else if (keypadCoroutine.currentKey == 'D' && mode == TOP_UP) {
      mode = CASH_OUT;
    } else if (mode == CASH_OUT) {
      mode = TOP_UP;
    } else {
      // do nothing
      continue;
    }

    displayCoroutine.requiresUpdate = true;
  }

  COROUTINE_END();
}

void MainCoroutine::resetBalance() {
  balance = {.deposit = 0, .total = 0};
  CardTransaction t = CardTransaction_init_zero;
  logCoroutine.transaction = t;
  displayCoroutine.requiresUpdate = true;
}
