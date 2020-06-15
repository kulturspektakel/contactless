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
  if (!request && !requestSuccessful && WiFi.status() == WL_CONNECTED) {
    getConfig();
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
  parseDate(time, request->respHeaderValue("Date"));
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

void Config::parseDate(char result[9], char* date) {
  // Sun, 17 Nov 2019 18:00:48 GMT
  result[0] = date[5];
  result[1] = date[6];
  result[2] = '0';
  result[3] = '0';
  result[4] = date[17];
  result[5] = date[18];
  result[6] = date[20];
  result[7] = date[21];
  result[8] = '\0';

  if (date[8] == 'J' && date[9] == 'a') {  // January
    result[3] = '1';
  } else if (date[8] == 'F') {  // February
    result[3] = '2';
  } else if (date[8] == 'M' && date[10] == 'r') {  // March
    result[3] = '3';
  } else if (date[8] == 'A' && date[9] == 'p') {  // April
    result[3] = '4';
  } else if (date[8] == 'M' && date[10] == 'y') {  // May
    result[3] = '5';
  } else if (date[8] == 'J' && date[10] == 'n') {  // June
    result[3] = '6';
  } else if (date[8] == 'J' && date[10] == 'l') {  // July
    result[3] = '7';
  } else if (date[8] == 'A' && date[9] == 'u') {  // August
    result[3] = '8';
  } else if (date[8] == 'S') {  // September
    result[3] = '9';
  } else if (date[8] == 'O') {  // October
    result[2] = '1';
    result[3] = '0';
  } else if (date[8] == 'N') {  // November
    result[2] = '1';
    result[3] = '1';
  } else if (date[8] == 'D') {  // December
    result[2] = '1';
    result[3] = '2';
  }
}

void Config::resetRetryCounter() {
  requestSuccessful = false;
  configDownloadRetries = CONFIG_DOWNLOAD_RETRIES;
}

void Config::updateSoftware() {
  // t_httpUpdate_return ret = ESPhttpUpdate.update(updateURL);
}
