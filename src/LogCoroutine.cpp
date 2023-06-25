#include <ArduinoLog.h>
#include <ConfigCoroutine.h>
#include <LogCoroutine.h>
#include <MainCoroutine.h>
#include <RFIDCoroutine.h>
#include <TimeEntryCoroutine.h>
#include <TimeLib.h>
#include <pb_encode.h>
#include "proto/product.pb.h"

extern ConfigCoroutine configCoroutine;
extern RFIDCoroutine rFIDCoroutine;
extern MainCoroutine mainCoroutine;
extern TimeEntryCoroutine timeEntryCoroutine;
extern char deviceID[9];
extern char deviceToken[48];
extern const int BUILD_NUMBER;

static const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static asyncHTTPrequest request;

int LogCoroutine::runCoroutine() {
  COROUTINE_BEGIN()

  dir = SD.open("/", FILE_READ);
  while (true) {
    file = dir.openNextFile();
    if (!file) {
      break;
    }
    if (isLogFile()) {
      filesToUpload++;
    }
    file.close();
  }
  dir.close();
  Log.infoln("[Log] has files to upload %d", filesToUpload);

  while (true) {
    COROUTINE_AWAIT(WiFi.status() == WL_CONNECTED && filesToUpload > 0);
    rFIDCoroutine.resetReader();  // needed to free SPI
    dir = SD.open("/", FILE_READ);
    dir.rewindDirectory();
    file = dir.openNextFile();
    while (WiFi.status() == WL_CONNECTED && filesToUpload > 0) {
      if (!file) {
        filesToUpload = 0;
        break;
      }
      if (!isLogFile()) {
        file.close();
        file = dir.openNextFile();
        continue;
      }
      Log.infoln("[Log] starting upload: %s", file.name());

      size_t len = file.size();
      if (len > LogMessage_size || len == 0) {
        // invalid file, ignore
        Log.error("[Log] %s was too large, deleting it", file.name());
        SD.remove(file.name());
        filesToUpload--;
        file = dir.openNextFile();
        continue;
      }

      file.read(data, len);
      request.open("POST", "http://api.kulturspektakel.de:51180/$$$/log");
      request.setReqHeader("x-ESP8266-STA-MAC", WiFi.macAddress().c_str());
      request.setReqHeader("x-ESP8266-Version", BUILD_NUMBER);
      request.setReqHeader("Authorization", deviceToken);
      request.send(data, len);

      COROUTINE_AWAIT(request.readyState() == 4);
      Log.infoln("[Log] upload finished: %s HTTP %d", file.name(),
                 request.responseHTTPcode());

      // Update time, if we got an actual HTTP response
      if (request.responseHTTPcode() > 0 && request.respHeaderExists("Date")) {
        timeEntryCoroutine.dateFromHTTP(request.respHeaderValue("Date"));
      }

      if (request.responseHTTPcode() == 201     // Successfully created
          || request.responseHTTPcode() == 400  // Invalid file
          || request.responseHTTPcode() == 409  // Already uploaded
      ) {
        rFIDCoroutine.resetReader();  // needed to free SPI
        if (!SD.remove(file.name())) {
          Log.errorln("[Log] %s could not be deleted", file.name());
        }
        filesToUpload--;
        file.close();
        file = dir.openNextFile();
      } else {
        Log.errorln("[Log] %s could not be uploaded, retrying in 15 seconds",
                    file.name());
        COROUTINE_DELAY_SECONDS(15);
        rFIDCoroutine.resetReader();  // needed to free SPI, otherwise loop
                                      // doesn't continue

        // TODO: because we are not moving to the next file, this file could
        // block the entire queue
      }
    }
    dir.close();
  }
  COROUTINE_END();
}

boolean LogCoroutine::isLogFile() {
  return file.isFile() && strlen(file.name()) == 12 &&
         strcmp(".log", &file.name()[8]) == 0;
}

// void dateTime(uint16_t* date, uint16_t* time) {
//   time_t t = now();
//   *date = FAT_DATE(t.year(), now.month(), now.day());
//   *time = FAT_TIME(now.hour(), now.minute(), now.second());
// }

int LogCoroutine::addProduct(int i) {
  if (!logMessage.has_order) {
    LogMessage_Order o = LogMessage_Order_init_zero;
    logMessage.order = o;
    logMessage.has_order = true;
  }

  logMessage.order.cart_items_count++;
  for (int j = 0; j < logMessage.order.cart_items_count; j++) {
    if (strcmp(logMessage.order.cart_items[j].product.name,
               configCoroutine.config.products[i].name) == 0) {
      return ++logMessage.order.cart_items[j].amount;
    }
  }

  // add to cart
  logMessage.order.cart_items[logMessage.order.cart_items_count - 1].amount = 1;
  logMessage.order.cart_items[logMessage.order.cart_items_count - 1]
      .has_product = true;
  logMessage.order.cart_items[logMessage.order.cart_items_count - 1].product =
      configCoroutine.config.products[i];
  return 1;
}

void LogCoroutine::writeLog(LogMessage_Order_PaymentMethod paymentMethod) {
  logMessage.device_time = now();
  logMessage.device_time_is_utc = timeEntryCoroutine.deviceTimeIsUtc;
  strncpy(logMessage.device_id, deviceID, 9);
  // generate client transaction ID
  for (size_t i = 0; i < sizeof(logMessage.client_id) - 1; i++) {
    // TODO Random not random
    logMessage.client_id[i] = charset[rand() % (int)(sizeof(charset) - 1)];
  }
  logMessage.client_id[sizeof(logMessage.client_id) - 1] = '\0';

  if (logMessage.has_order) {
    logMessage.order.payment_method = paymentMethod;
    if (configCoroutine.config.list_id > 0) {
      logMessage.order.list_id = configCoroutine.config.list_id;
      logMessage.order.has_list_id = true;
    }
  }

  if (paymentMethod == LogMessage_Order_PaymentMethod_KULT_CARD) {
    logMessage.has_card_transaction = true;

    LogMessage_CardTransaction transaction =
        LogMessage_CardTransaction_init_zero;
    logMessage.card_transaction = transaction;

    switch (mainCoroutine.mode) {
      case CASH_OUT:
        logMessage.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_CASHOUT;
        break;
      case CHARGE_LIST:
      case CHARGE_MANUAL:
        logMessage.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_CHARGE;
        break;
      case TOP_UP:
        logMessage.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_TOP_UP;
        break;
      default: {
        // other events are not logged
        return;
      }
    }
    strncpy(logMessage.card_transaction.card_id, rFIDCoroutine.cardId,
            sizeof(logMessage.card_transaction.card_id));
    logMessage.card_transaction.balance_before =
        rFIDCoroutine.cardValueBefore.total;
    logMessage.card_transaction.deposit_before =
        rFIDCoroutine.cardValueBefore.deposit;
    logMessage.card_transaction.balance_after =
        rFIDCoroutine.cardValueAfter.total;
    logMessage.card_transaction.deposit_after =
        rFIDCoroutine.cardValueAfter.deposit;

    if (rFIDCoroutine.ultralightCounter != 0) {
      logMessage.card_transaction.counter = rFIDCoroutine.ultralightCounter;
      logMessage.card_transaction.has_counter = true;
    }
  }

  // write log file
  char filename[13];
  sprintf(filename, "%s.log", logMessage.client_id);

  rFIDCoroutine.resetReader();  // needed to free SPI
  // SD.dateTimeCallback(dateTime);
  File logFile = SD.open(filename, FILE_WRITE);
  if (logFile && logFile.availableForWrite()) {
    uint8_t buffer[LogMessage_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(logMessage));
    pb_encode(&stream, LogMessage_fields, &logMessage);
    size_t written = logFile.write(buffer, stream.bytes_written);
    Log.infoln("[Log] Written logfile %s for counter %d (%d bytes)", filename,
               rFIDCoroutine.ultralightCounter, written);
    filesToUpload++;
  } else {
    Log.errorln("[Log] Could not create logfile %s", filename);
  }
}
