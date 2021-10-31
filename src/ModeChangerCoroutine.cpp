#include "ModeChangerCoroutine.h"

#include <ArduinoLog.h>
#include "ConfigCoroutine.h"
#include "DisplayCoroutine.h"
#include "MainCoroutine.h"
#include "RFIDCoroutine.h"

extern MainCoroutine mainCoroutine;
extern ConfigCoroutine configCoroutine;
extern RFIDCoroutine rFIDCoroutine;
extern DisplayCoroutine displayCoroutine;
extern const char* MODE_CHANGER;

int ModeChangerCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT(isModeChanger() && lastModeChange < millis() - 1500);
    if (mainCoroutine.mode == TOP_UP) {
      mainCoroutine.mode = configCoroutine.config.products_count > 0
                               ? CHARGE_LIST
                               : CHARGE_MANUAL;
    } else {
      mainCoroutine.mode = TOP_UP;
    }
    displayCoroutine.requiresUpdate = true;
    lastModeChange = millis();
  }
}

bool ModeChangerCoroutine::isModeChanger() {
  if (strlen(rFIDCoroutine.cardId) == 0) {
    return false;
  }

  for (size_t i = 0; i < strlen(MODE_CHANGER); i += 9) {
    if (strncmp(rFIDCoroutine.cardId, &MODE_CHANGER[i], 8) == 0) {
      return true;
    }
  }
  return false;
}
