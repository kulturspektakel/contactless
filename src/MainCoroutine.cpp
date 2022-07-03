#include "MainCoroutine.h"
#include <ArduinoLog.h>
#include <ESP8266WiFi.h>
#include <TimeLib.h>
#include <pb_decode.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "KeypadCoroutine.h"
#include "LogCoroutine.h"
#include "ProductNumberCoroutine.h"
#include "proto/logMessage.pb.h"
#include "proto/product.pb.h"

extern DisplayCoroutine displayCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern ProductNumberCoroutine productNumberCoroutine;
extern LogCoroutine logCoroutine;
extern bool string16(pb_istream_t* stream,
                     const pb_field_t* field,
                     void** buffer);

int MainCoroutine::runCoroutine() {
  COROUTINE_BEGIN();

  COROUTINE_AWAIT(timeStatus() == timeSet);
  defaultMode();
  displayCoroutine.requiresUpdate = true;

  while (true) {
    COROUTINE_AWAIT(keypadCoroutine.currentKey);

    bool tripplePress = isTripplePress();
    bool isDigit =
        keypadCoroutine.currentKey >= '0' && keypadCoroutine.currentKey <= '9';

    if (keypadCoroutine.currentKey == 'D' && mode == TOP_UP && !balance) {
      mode = CASH_OUT;
    } else if (keypadCoroutine.currentKey == 'D' &&
               (mode == TOP_UP || mode == CHARGE_MANUAL ||
                mode == CHARGE_LIST) &&
               balance) {
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
      addProduct(keypadCoroutine.currentKey - '1');
    } else if (keypadCoroutine.currentKey == '*' && balance &&
               (mode == CHARGE_LIST || mode == CHARGE_MANUAL)) {
      mode = CHARGE_WITHOUT_CARD;
    } else if (mode == CHARGE_LIST && keypadCoroutine.currentKey == '#') {
      mode = tripplePress ? CHARGE_MANUAL : PRODUCT_NUMBER_ENTRY;
    } else if (mode == CHARGE_MANUAL && keypadCoroutine.currentKey == '#') {
      defaultMode();
    } else if (tripplePress && keypadCoroutine.currentKey == '#' &&
               mode == TOP_UP) {
      mode = FIX_CARD;
    } else if (mode == PRODUCT_NUMBER_ENTRY && !isDigit) {
      defaultMode();
      productNumberCoroutine.reset();
    } else if (mode == CASH_OUT) {
      mode = TOP_UP;
    } else if (tripplePress && keypadCoroutine.currentKey == '*' && !balance) {
      mode = DEBUG_INFO;
    } else if (mode == FIX_CARD) {
      defaultMode();
    } else {
      // do nothing
      continue;
    }

    displayCoroutine.requiresUpdate = true;
  }

  COROUTINE_END();
}

void MainCoroutine::resetBalance() {
  balance.reset();
  LogMessage l = LogMessage_init_zero;
  logCoroutine.logMessage = l;
  displayCoroutine.requiresUpdate = true;
}

bool MainCoroutine::addProduct(uint8_t index) {
  if (index < sizeof(configCoroutine.config.products) /
                  sizeof(*configCoroutine.config.products) &&
      configCoroutine.config.products[index].price > 0) {
    balance.total += configCoroutine.config.products[index].price;
    int amount = logCoroutine.addProduct(index);
    if (amount > 1) {
      char line1[27];
      snprintf(line1, sizeof(line1), "%dx %s", amount,
               configCoroutine.config.products[index].name);
      displayCoroutine.show(
          line1, nullptr, -2000,
          configCoroutine.config.products[index].price * amount);
    } else {
      displayCoroutine.show(configCoroutine.config.products[index].name,
                            nullptr, -2000,
                            configCoroutine.config.products[index].price);
    }
    return true;
  } else {
    return false;
  }
}

bool MainCoroutine::isTripplePress() {
  if (keypadCoroutine.currentKey == trippleChar) {
    if (trippleTimings[1] == 0 && trippleTimings[0] > millis() - 500) {
      trippleTimings[1] = millis();
    } else if (trippleTimings[1] != 0 && trippleTimings[1] > millis() - 500) {
      resetTripplePress();
      Log.infoln("[Main] Tripple press triggered");
      return true;
    } else {
      resetTripplePress();
    }
  } else {
    resetTripplePress();
  }
  return false;
}

void MainCoroutine::resetTripplePress() {
  trippleTimings[0] = millis();
  trippleTimings[1] = 0;
  trippleChar = keypadCoroutine.currentKey;
}

void MainCoroutine::defaultMode() {
  if (canTopUp) {
    mode = TOP_UP;
  } else {
    mode =
        configCoroutine.config.products_count > 0 ? CHARGE_LIST : CHARGE_MANUAL;
  }
  displayCoroutine.requiresUpdate = true;
}
