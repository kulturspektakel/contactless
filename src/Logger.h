#pragma once

#include <Arduino.h>
#include <SdFat.h>
#include <asyncHTTPrequest.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include "proto/transaction.pb.h"

class StateManager;

class Logger {
  int pin;
  StateManager& stateManager;
  int cachedNumberPendingUploads = -1;

  bool havingFilesToUpload =
      true;  // always assuming there might be some in the beginning
  void uploadLog(sdfat::File* file);
  void writeLog(char* logLine);
  asyncHTTPrequest* uploadRequest;
  bool uploadInProgress;
  bool isLogFile(sdfat::File* file);
  uint8_t logData[TransactionMessage_size];

 public:
  char uploadingFileName[13];  // 8 + 1 + 3 + 1
  void uploadFinished();
  void noMoreUploads();

  Logger(StateManager& stateManager, sdfat::SdFat& sd);
  void setup();
  void loop();
  void log();
  void writeProducts(char productNames[9][17], int productPrices[9]);
  int numberPendingUploads();
  int numberPendingUploadsCached(bool resetCache);

  sdfat::SdFat& sdCard;
};
