#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <inttypes.h>
#include <array>

class StateManager;

class Keypad {
  StateManager& stateManager;
  int address;
  int currentRow;
  int currentData;
  long trippleHash[3];
  long lastKeyTime;

  char get_key();
  void pcf8574Write(int, int);
  int pcf8574Read(int);
  void handleKey(char key);
  void poundKey();

 public:
  Keypad(StateManager& stateManager, int address);
  void setup();
  void loop();
};
