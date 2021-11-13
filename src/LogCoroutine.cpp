#include <ArduinoLog.h>
#include <ConfigCoroutine.h>
#include <LogCoroutine.h>
#include <RFIDCoroutine.h>
#include <TimeLib.h>
#include <pb_encode.h>
#include "proto/product.pb.h"

using namespace sdfat;

extern ConfigCoroutine configCoroutine;
extern RFIDCoroutine rFIDCoroutine;
extern char deviceID[9];
extern char deviceToken[48];

static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static asyncHTTPrequest request;

int LogCoroutine::runCoroutine() {
  COROUTINE_BEGIN()
  // rFIDCoroutine.resetReader();  // needed to free SPI
  dir = SD.open("/", FILE_READ);
  while (true) {
    file = dir.openNextFile();
    if (!file) {
      break;
    }
    if (isLogFile()) {
      logsToUpload++;
    }
    file.close();
  }
  dir.close();
  Log.infoln("[Log] %d files to upload", logsToUpload);

  while (true) {
    COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED && logsToUpload > 0);
    rFIDCoroutine.resetReader();  // needed to free SPI
    dir = SD.open("/", FILE_READ);
    while (true) {
      if (file) {
        file.close();
      }
      file = dir.openNextFile();
      if (!file) {
        break;
      }
      if (isLogFile()) {
        Log.infoln("[Log] starting upload: %s", file.name());

        size_t len = file.size();
        if (len > CardTransaction_size) {
          Log.error("[Log] %s was too large, deleting it", file.name());
          SD.remove(file.name());
          logsToUpload--;
          continue;
        }

        file.read(data, len);
        request.open("POST", "http://api.kulturspektakel.de:51180/$$$/log");
        request.setReqHeader("x-ESP8266-STA-MAC", WiFi.macAddress().c_str());
        request.setReqHeader("Authorization", deviceToken);
        request.send(data, len);

        COROUTINE_AWAIT(request.readyState() == 4);
        Log.infoln("[Log] upload finished: %s HTTP %d", file.name(),
                   request.responseHTTPcode());
        if (request.responseHTTPcode() == 200 ||
            request.responseHTTPcode() == 201 ||
            request.responseHTTPcode() == 400) {
          rFIDCoroutine.resetReader();  // needed to free SPI
          SD.remove(file.name());
          logsToUpload--;
        }
      }
    }
    dir.close();
    logsToUpload = 0;
  }
  COROUTINE_END();
}

boolean LogCoroutine::isLogFile() {
  return file.isFile() && strlen(file.name()) == 12 &&
         strcmp(".log", &file.name()[8]) == 0;
}

void LogCoroutine::addProduct(int i) {
  // for (int j = 0; j < transaction.cart_items_count; j++) {
  //   Log.infoln("[Log] cmp %s %s", transaction.cart_items[j].product.name,
  //              configCoroutine.config.products[i].name);

  //   if (strcmp(transaction.cart_items[j].product.name,
  //              configCoroutine.config.products[i].name) == 0) {
  //     transaction.cart_items[i].amount++;
  //     return;
  //   }
  // }

  // add to cart
  transaction.cart_items[transaction.cart_items_count].amount = 1;
  transaction.cart_items[transaction.cart_items_count].has_product = true;
  transaction.cart_items[transaction.cart_items_count].product =
      configCoroutine.config.products[i];
  transaction.cart_items_count++;
}

void LogCoroutine::writeLog() {
  transaction.device_time = now();
  transaction.payment_method = CardTransaction_PaymentMethod_KULT_CARD;
  strncpy(transaction.device_id, deviceID, 9);

  // generate client transaction ID
  for (size_t i = 0; i < sizeof(transaction.client_id) - 1; i++) {
    transaction.client_id[i] = charset[rand() % (int)(sizeof(charset) - 1)];
  }
  transaction.client_id[sizeof(transaction.client_id) - 1] = '\0';

  // write log file
  char filename[13];
  sprintf(filename, "%s.log", transaction.client_id);

  rFIDCoroutine.resetReader();  // needed to free SPI
  File logFile = SD.open(filename, FILE_WRITE);
  if (logFile && logFile.availableForWrite()) {
    uint8_t buffer[CardTransaction_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, CardTransaction_size);
    boolean a = pb_encode(&stream, CardTransaction_fields, &transaction);
    Log.errorln("bool %d %s", a ? 1 : 0, stream.errmsg);
    logFile.write(buffer, CardTransaction_size);
    Log.infoln("[Log] Written logfile %s", filename);
    logsToUpload++;
  } else {
    Log.errorln("[Log] Could not create logfile %s", filename);
  }
}
