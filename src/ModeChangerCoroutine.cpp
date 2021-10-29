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
extern std::vector<std::string> MODE_CHANGER;

int ModeChangerCoroutine::runCoroutine() {
  COROUTINE_LOOP() {
    COROUTINE_AWAIT(isModeChanger() && millis() + 1500 < lastModeChange);
    Serial.println("ass");
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
  for (std::string id : MODE_CHANGER) {
    if (strcmp(rFIDCoroutine.cardId, id.c_str()) == 0) {
      return true;
    }
  }
  return false;
}
