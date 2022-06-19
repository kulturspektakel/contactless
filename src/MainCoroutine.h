#pragma once

#include <AceRoutine.h>
#include <Arduino.h>
#include <hd44780.h>

using namespace ace_routine;

enum Mode {
  TIME_ENTRY,
  CHARGE_MANUAL,
  CHARGE_LIST,
  TOP_UP,
  CASH_OUT,
  INITIALIZE_CARD,
  SOFTWARE_UPDATE,
  CHARGE_WITHOUT_CARD,
  PRODUCT_NUMBER_ENTRY,
  DEBUG_INFO,
};

struct Balance_t {
  int deposit;
  int total;

  operator bool() const { return (0 != deposit || 0 != total); }
  void reset() {
    total = 0;
    deposit = 0;
  }
} const Balance_default = {.deposit = 0, .total = 0};

typedef struct Balance_t Balance;

class MainCoroutine : public Coroutine {
 private:
  char trippleChar = '\0';
  unsigned long trippleTimings[2];
  void resetTripplePress();
  bool isTripplePress();

 public:
  int runCoroutine() override;
  void resetBalance();
  void changeMode();
  void defaultMode();
  bool addProduct(uint8_t index);
  Mode mode = TIME_ENTRY;
  Balance balance = Balance_default;
};