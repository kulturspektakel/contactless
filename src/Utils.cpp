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

  char ua[15];
  String id = WiFi.macAddress().substring(9);
  snprintf(ua, 15, "%s/%d", id.c_str(), BUILD_NUMBER);
  request->setReqHeader("User-Agent", ua);

  char authorization[48];
  String preToken = id + SALT;
  snprintf(authorization, 48, "Bearer %s",
           sha1(preToken.c_str(), preToken.length()).c_str());
  request->setReqHeader("Authorization", authorization);
}
}  // namespace Utils
