#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SdFat.h>
#include <asyncHTTPrequest.h>

class StateManager;

const int RECONNECT_INTERVAL = 10 * 1000;

class Network {
  wl_status_t wifiStatus = WL_DISCONNECTED;
  unsigned long nextReconnect = millis() + RECONNECT_INTERVAL;

 public:
  Network(StateManager& stateManager);
  StateManager& stateManager;
  ~Network();
  void setup();
  void loop();
  void tryToConnect();
};
