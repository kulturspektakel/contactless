#include "Logger.h"
#include <ESP8266WiFi.h>
#include <TimeLib.h>
#include "StateManager.h"
#include "Utils.h"

Logger::Logger(StateManager& sm, sdfat::SdFat& sd)
    : stateManager(sm), sdCard(sd) {}

void Logger::setup() {}

void Logger::loop() {
  if (WiFi.status() != WL_CONNECTED || uploadInProgress ||
      !havingFilesToUpload) {
    return;
  }

  sdfat::File root = sdCard.open("/");
  sdfat::File uploadingFile;
  root.rewindDirectory();

  // find next log file to upload
  while (true) {
    if (uploadingFile) {
      uploadingFile.close();
    }
    uploadingFile = root.openNextFile();
    if (!uploadingFile) {
      // no more files
      break;
    }

    if (isLogFile(&uploadingFile)) {
      // found file to upload
      uploadingFile.getName(uploadingFileName, sizeof(uploadingFileName));
      break;
    }
  }

  root.close();

  if (uploadingFile) {
    uploadLog(&uploadingFile);
    uploadingFile.close();
  } else {
    noMoreUploads();
  }
}

void Logger::log() {
  char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  char filename[] = "00000000.log";
  for (int i = 0; i < 8; i++) {
    int pos = (int)random(0, strlen(alphabet));
    filename[i] = alphabet[pos];
  }

  strlcpy(stateManager.state.transaction.id, filename, 9);
  strcpy(stateManager.state.transaction.device_id,
         WiFi.macAddress().substring(9).c_str());
  stateManager.state.transaction.device_time = now();
  strcpy(stateManager.state.transaction.list_name,
         stateManager.state.config.name);

  sdCard.freeClusterCount();  // otherwise writes sometimes fail?
  sdfat::File logFile = sdCard.open(filename, sdfat::O_RDWR | sdfat::O_CREAT);
  bool status = Utils::writeToFile(&logFile, TransactionMessage_fields,
                                   &stateManager.state.transaction);
  logFile.close();

  if (status) {
    havingFilesToUpload = true;
    Serial.print("[LOGGER] Log written to ");
  } else {
    Serial.print("[LOGGER] Could not write log ");
  }
  Serial.println(filename);
}

void Logger::uploadFinished() {
  uploadInProgress = false;
}

void Logger::noMoreUploads() {
  havingFilesToUpload = false;
}

void requestHandler(void* instance, asyncHTTPrequest* request, int readyState) {
  if (readyState != 4) {
    return;
  }

  int responseCode = request->responseHTTPcode();
  Serial.print("[LOGGER] log upload HTTP status: ");
  Serial.println(responseCode);

  delete request;
  bool deleteLog = false;
  bool deleted = false;

  if (responseCode == HTTPCODE_NOT_CONNECTED) {
    WiFi.reconnect();
  } else if (responseCode < 300 && responseCode >= 200) {
    Serial.print("[LOGGER] log upload successfully: ");
    Serial.println(((Logger*)instance)->uploadingFileName);
    deleteLog = true;
  } else if (responseCode == 400) {
    // invalid log file, delete it
    deleteLog = true;
    Serial.println("[LOGGER] Deleting invalid log");
  }

  if (deleteLog) {
    // for some reasons sdCard.remove sometimes fails, but succeeds in
    // subsequent trys
    for (int retries = 5; retries > 0 && !deleted; retries--) {
      deleted = ((Logger*)instance)
                    ->sdCard.remove(((Logger*)instance)->uploadingFileName);
    }
    if (!deleted) {
      Serial.println("[LOGGER] deletion failed");
      // to prevent upload loop
      ((Logger*)instance)->noMoreUploads();
    }
  }

  ((Logger*)instance)->uploadFinished();
}

void Logger::uploadLog(sdfat::File* file) {
  if (uploadInProgress) {
    return;
  }
  uploadInProgress = true;

  Serial.print("[LOGGER] start upload of ");
  Serial.println(uploadingFileName);

  size_t len = file->readBytes(logData, TransactionMessage_size);
  Logger::uploadRequest = new asyncHTTPrequest();
  uploadRequest->onReadyStateChange(requestHandler, (void*)this);
  Utils::configureRequest(uploadRequest, APIEndpoint::LOGGING);
  uploadRequest->send(logData, len);
}

int Logger::numberPendingUploads() {
  // this is required to make sure we are reading all files
  sdCard.freeClusterCount();

  int count = 0;
  sdfat::File root = sdCard.open("/");
  if (!root) {
    return -1;
  }
  root.rewindDirectory();
  sdfat::File file;
  while (true) {
    file = root.openNextFile();
    if (!file) {
      break;
    }

    if (isLogFile(&file)) {
      count++;
    }
    file.close();
  }
  root.close();
  cachedNumberPendingUploads = count;
  return count;
}

bool Logger::isLogFile(sdfat::File* file) {
  char name[13];
  file->getName(name, 13);
  return name[8] == '.' && name[9] == 'l' && name[10] == 'o' && name[11] == 'g';
}

int Logger::numberPendingUploadsCached(bool resetCache) {
  // cached method to lower number of SD card requests
  if (resetCache) {
    numberPendingUploads();
  }
  return cachedNumberPendingUploads;
}
