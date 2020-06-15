#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SdFat.h>
#include <asyncHTTPrequest.h>
#include "proto/config.pb.h"

class StateManager;

const int CONFIG_DOWNLOAD_RETRIES = 5;

class Config {
  bool requestSuccessful = false;

  sdfat::SdFat& sdCard;
  void readProducts();
  void parseDate(char result[9], char* date);
  int configDownloadRetries = CONFIG_DOWNLOAD_RETRIES;
  const char* productsFile = "_config.cfg";

 public:
  Config(StateManager& stateManager, sdfat::SdFat& sdCard);
  StateManager& stateManager;
  asyncHTTPrequest* request;
  ~Config();
  void setup();
  void loop();
  void getConfig();
  void receivedConfig();
  void resetRetryCounter();
  void updateSoftware();
};
