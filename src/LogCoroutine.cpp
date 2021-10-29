#include <ArduinoLog.h>
#include <ConfigCoroutine.h>
#include <LogCoroutine.h>
#include <TimeLib.h>
#include <pb_encode.h>
#include "proto/product.pb.h"

using namespace sdfat;

extern ConfigCoroutine configCoroutine;
extern SdFat sd;
extern char deviceID[9];
extern char deviceToken[48];

static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static asyncHTTPrequest request;

int LogCoroutine::runCoroutine() {
  COROUTINE_BEGIN()
  sd.vwd()->rewind();
  while (file.openNext(sd.vwd(), O_READ)) {
    if (isLogFile()) {
      logsToUpload++;
    }
    file.close();
  }
  Log.infoln("[Log] %d files to upload", logsToUpload);

  while (true) {
    COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED && logsToUpload > 0);
    sd.vwd()->rewind();
    while (file.openNext(sd.vwd(), O_READ)) {
      if (isLogFile()) {
        Log.infoln("[Log] starting upload: %s", filename);

        size_t len = file.fileSize();
        file.readBytes(data, len);
        request.open("POST", "http://api.kulturspektakel.de:51180/$$$/log");
        request.setReqHeader("x-ESP8266-STA-MAC", deviceID);
        request.setReqHeader("Authorization", deviceToken);
        request.send(data, len);

        COROUTINE_AWAIT(request.readyState() == 4);
        Log.infoln("[Log] upload successful: %s HTTP %d", filename,
                   request.responseHTTPcode());
        if (request.responseHTTPcode() >= 200 &&
            request.responseHTTPcode() < 500) {
          sd.remove(filename);
          logsToUpload--;
        }
      }
      file.close();
    }
    logsToUpload = 0;
  }
  COROUTINE_END();
}

boolean LogCoroutine::isLogFile() {
  file.getName(filename, 13);
  return file.isFile() && strcmp(".log", &filename[8]) == 0;
}

void LogCoroutine::addProduct(int i) {
  for (int j = 0; j < transaction.cart_items_count; j++) {
    Log.infoln("[Log] cmp %s %s", transaction.cart_items[j].product.name,
               configCoroutine.config.products[i].name);

    if (strcmp(transaction.cart_items[j].product.name,
               configCoroutine.config.products[i].name) == 0) {
      transaction.cart_items[i].amount++;
      return;
    }
  }

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

  sdfat::File logFile = sd.open(filename, O_WRITE | O_CREAT);
  if (logFile && logFile.isOpen()) {
    uint8_t buffer[CardTransaction_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, CardTransaction_size);
    pb_encode(&stream, CardTransaction_fields, &transaction);
    logFile.write(buffer, CardTransaction_size);
    Log.infoln("[Log] Written logfile %s", filename);
    logsToUpload++;
  } else {
    Log.errorln("[Log] Could not create logfile %s", filename);
  }
}
