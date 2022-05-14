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
};

struct Balance_t {
  uint8_t deposit;
  uint16_t total;

  operator bool() const { return (0 != deposit || 0 != total); }
  void reset() {
    total = 0;
    deposit = 0;
  }
} const Balance_default = {.deposit = 0, .total = 0};

typedef struct Balance_t Balance;

class MainCoroutine : public Coroutine {
 private:
  unsigned long lastModeChange = 0;

 public:
  int runCoroutine() override;
  void resetBalance();
  void changeMode();
  Mode mode = TIME_ENTRY;
  Balance balance = Balance_default;
};