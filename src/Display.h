#pragma once
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

class StateManager;

class Display {
  LiquidCrystal_I2C* lcd;
  unsigned long messageUntil;
  StateManager& stateManager;
  int currentDebugPage = -1;

  void formatCurrency(char arr[17], int value);
  void home();
  void clearMessage();

 public:
  Display(StateManager& stateManager, int address);
  ~Display();
  void setup();
  void loop();
  void message(const char* message, int duration);
  void messageWithPrice(const char* message, int price, int duration);
  void updateScreen(bool clear);
  void showBalance(int duration);
  void showCashout(int cashout);
  void showDebug(bool startDebugMode, int pendingLogs);
};
