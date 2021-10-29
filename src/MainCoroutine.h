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
  INITIALIZE_CARD
};

struct Balance {
  int deposit;
  int total;
};

class MainCoroutine : public Coroutine {
 private:
  unsigned long lastModeChange = 0;

 public:
  int runCoroutine() override;
  void resetBalance();
  void changeMode();
  Mode mode = TIME_ENTRY;
  Balance balance = {.deposit = 0, .total = 0};
};