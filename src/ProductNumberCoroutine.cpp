#include "ProductNumberCoroutine.h"

#include <ArduinoLog.h>
#include <ConfigCoroutine.h>
#include <DisplayCoroutine.h>
#include <KeypadCoroutine.h>
#include <MainCoroutine.h>

extern MainCoroutine mainCoroutine;
extern KeypadCoroutine keypadCoroutine;
extern ConfigCoroutine configCoroutine;
extern DisplayCoroutine displayCoroutine;

int ProductNumberCoroutine::runCoroutine() {
  COROUTINE_BEGIN();
  while (true) {
    entry[0] = '#';
    pointer = 1;
    for (uint8_t i = pointer; i <= PRODUCT_NUMBER_LENGTH; i++) {
      entry[i] = '_';
    }
    COROUTINE_AWAIT(mainCoroutine.mode == PRODUCT_NUMBER_ENTRY);
    for (; pointer <= PRODUCT_NUMBER_LENGTH; pointer++) {
      COROUTINE_AWAIT(keypadCoroutine.currentKey);
      entry[pointer] = '_';
      entry[pointer] = keypadCoroutine.currentKey;
      displayCoroutine.requiresUpdate = true;
    }
    COROUTINE_DELAY(200);
    uint8_t base = 1;
    uint8_t index = -1;
    for (uint8_t i = PRODUCT_NUMBER_LENGTH; i > 0; i--) {
      index += (entry[i] - '0') * base;
      base *= 10;
    }
    if (!mainCoroutine.addProduct(index)) {
      char line1[17];
      sprintf(line1, "Produkt %s", entry);
      displayCoroutine.show(line1, "nicht gefunden", -2000);
    }
    COROUTINE_YIELD();
    mainCoroutine.defaultMode();
  }
  COROUTINE_END();
}
