#include "Utils.h"
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <SdFat.h>
#include "Buildnumber.h"
#include "proto/transaction.pb.h"

extern const char* SALT;

namespace Utils {

bool callback(pb_ostream_t* stream, const uint8_t* buf, size_t count) {
  sdfat::File* file = (sdfat::File*)stream->state;
  return file->write(buf, count) == count;
}

bool writeToFile(sdfat::File* file,
                 const pb_msgdesc_t* fields,
                 const void* src_struct) {
  if (!file || !file->isOpen()) {
    Serial.println("[LOGGER] Can not write to SD");
    return false;
  }

  pb_ostream_t stream = {&callback, file, SIZE_MAX, 0};
  bool status = pb_encode(&stream, fields, src_struct);

  if (!status) {
    Serial.print("[UTILS] Protobuf error: ");
    Serial.println(stream.errmsg);
  }

  file->close();
  return status;
}

void configureRequest(asyncHTTPrequest* request, APIEndpoint api) {
  request->setTimeout(15);
  switch (api) {
    case CONFIG:
      request->open("GET", "kult.cash:51180/api/config");
      break;
    case LOGGING:
      request->open("POST", "kult.cash:51180/api/log");
      break;
  }

  request->setReqHeader("x-ESP8266-STA-MAC", WiFi.macAddress().c_str());
  request->setReqHeader("x-ESP8266-version", BUILD_NUMBER);

  char authorization[48];
  char token[41];
  getToken(token);
  snprintf(authorization, 48, "Bearer %s", token);
  request->setReqHeader("Authorization", authorization);
}

void getToken(char token[41]) {
  uint32_t len = strlen(SALT) + 9;
  char preToken[len];
  char id[9];
  getID(id);
  snprintf(preToken, len, "%s%s", id, SALT);
  uint8_t hash[20];
  sha1(preToken, len - 1, hash);
  char* ptr = &token[0];
  for (int i = 0; i < 20; i++) {
    ptr += sprintf(ptr, "%02hhx", hash[i]);
  }
  token[40] = '\0';
}

void getID(char id[9]) {
  snprintf(id, 9, "%s", WiFi.macAddress().substring(9).c_str());
}

void parseDate(char result[9], char* date) {
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
}  // namespace Utils
