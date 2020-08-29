#include "Config.h"
#include <ESP8266httpUpdate.h>
#include "StateManager.h"
#include "Utils.h"

Config::Config(StateManager& sm, sdfat::SdFat& sd)
    : stateManager(sm), sdCard(sd) {}

Config::~Config() {
  delete request;
}

void Config::setup() {
  readProducts();
}

void Config::loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!request && !requestSuccessful) {
      getConfig();
    }
    if (shouldRequestSoftwareUpdate) {
      requestSoftwareUpdate();
    }
  }
}

void Config::requestSoftwareUpdate() {
  char token[41];
  Utils::getToken(token);
  char url[81];
  snprintf(url, 81, "http://kult.cash:51180/api/update?token=%s", token);
  switch (ESPhttpUpdate.update(url, 1)) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[CONFIG] Update error (%d): %s\n",
                    ESPhttpUpdate.getLastError(),
                    ESPhttpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[CONFIG] no update");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("[CONFIG] got update");
      ESP.restart();
      break;
  }
}

void Config::readProducts() {
  sdfat::File file = sdCard.open(productsFile, sdfat::O_RDONLY);
  if (file && file.isOpen()) {
    Serial.println("[CONFIG] read config from SD");
    uint8_t data[ConfigMessage_size];
    size_t len = file.readBytes(data, ConfigMessage_size);
    file.close();
    stateManager.updateConfig(data, len);
  } else {
    Serial.println("[CONFIG] producs file does not exist");
    stateManager.welcome();
  }
}

void completionHandler(void* instance,
                       asyncHTTPrequest* request,
                       int readyState) {
  if (readyState != 4) {
    return;
  }

  int responseCode = request->responseHTTPcode();
  Serial.print("[CONFIG] HTTP status: ");
  Serial.println(responseCode);

  if (responseCode == HTTPCODE_NOT_CONNECTED) {
    WiFi.reconnect();
  } else if (responseCode >= 200 && responseCode < 300) {
    ((Config*)instance)->receivedConfig();
  }

  delete request;
  ((Config*)instance)->request = nullptr;
  if (responseCode == HTTPCODE_TIMEOUT) {
    ((Config*)instance)->getConfig();
  }
}

void Config::getConfig() {
  configDownloadRetries--;
  if (configDownloadRetries < 1) {
    return;
  }
  Serial.println("[CONFIG] requesting config");
  request = new asyncHTTPrequest();
  request->onReadyStateChange(completionHandler, (void*)this);
  Utils::configureRequest(request, APIEndpoint::CONFIG);
  request->send();
}

void Config::receivedConfig() {
  requestSuccessful = true;

  // update time
  char time[9];
  Utils::parseDate(time, request->respHeaderValue("Date"));
  stateManager.receivedTime(time, true);

  if (request->responseHTTPcode() == 200) {
    Serial.println("[CONFIG] received config");
    size_t len = request->responseLength();
    uint8_t buffer[len];
    request->responseRead(buffer, len);
    bool updated = stateManager.updateConfig(buffer, len);
    if (!updated) {
      Serial.println("[CONFIG] config same as before");
      return;
    }
  }

  sdfat::File file = sdCard.open(
      productsFile, sdfat::O_RDWR | sdfat::O_CREAT | sdfat::O_AT_END);
  if (sdCard.remove(productsFile)) {
    Serial.println("[CONFIG] removed old config");
  }
  if (file) {
    file.close();
  }

  if (request->responseHTTPcode() == 200) {
    sdfat::File file =
        sdCard.open(productsFile, sdfat::O_RDWR | sdfat::O_CREAT);
    bool status = Utils::writeToFile(&file, ConfigMessage_fields,
                                     &stateManager.state.config);
    file.close();
    if (status) {
      Serial.println("[CONFIG] saved config");
    } else {
      Serial.println("[CONFIG] writing config failed");
    }
  }
}

void Config::resetRetryCounter() {
  requestSuccessful = false;
  configDownloadRetries = CONFIG_DOWNLOAD_RETRIES;
}

void Config::updateSoftware() {
  shouldRequestSoftwareUpdate = true;
}
