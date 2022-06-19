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
  entry[0] = '#';
  entry[1] = '_';
  entry[2] = '_';
  COROUTINE_LOOP() {
    COROUTINE_AWAIT(mainCoroutine.mode == PRODUCT_NUMBER_ENTRY);
    COROUTINE_AWAIT(keypadCoroutine.currentKey);
    entry[1] = keypadCoroutine.currentKey;
    displayCoroutine.requiresUpdate = true;
    COROUTINE_AWAIT(keypadCoroutine.currentKey);
    entry[2] = keypadCoroutine.currentKey;
    displayCoroutine.requiresUpdate = true;
    COROUTINE_DELAY(200);
    uint8_t index = (entry[1] - '0') * 10 + (entry[2] - '0') - 1;
    mainCoroutine.addProduct(index);
    COROUTINE_YIELD();
    mainCoroutine.defaultMode();
  }
}
