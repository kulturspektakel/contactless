#pragma once
#include <Arduino.h>
#include <string>

#include "Buzzer.h"
#include "Config.h"
#include "Display.h"
#include "Logger.h"
#include "Network.h"
#include "State.h"

class StateManager {
  Logger* logger;
  Buzzer* buzzer;
  Display* display;
  Network* network;
  Config* config;

 public:
  StateManager();
  void setup(Display* d, Buzzer* b, Logger* l, Network* n, Config* c);

  void keyPress(char key);
  void error(const char* message);
  void showBalance();
  void chargeCard(int newValue, int newTokens);
  void cashoutCard();
  void cardInitialized();
  void receivedTime(const char* time, bool isUTC);
  void toggleChargerMode();
  bool updateConfig(uint8_t* buffer, size_t len);
  void selectProduct(int index);
  void stateUpdated();
  void resetState();
  void showDebug(bool startDebugMode);
  void clearCart();
  void welcome();
  void reload();

  State state = {
      {0},                              // entry
      0,                                // value
      0,                                // tokens
      true,                             // manualEntry
      ConfigMessage_init_default,       // config
      TransactionMessage_init_default,  // transaction
  };
};
