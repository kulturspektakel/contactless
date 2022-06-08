#include <ArduinoLog.h>
#include <ConfigCoroutine.h>
#include <LogCoroutine.h>
#include <MainCoroutine.h>
#include <RFIDCoroutine.h>
#include <TimeEntryCoroutine.h>
#include <TimeLib.h>
#include <pb_encode.h>
#include "proto/product.pb.h"

using namespace sdfat;

extern ConfigCoroutine configCoroutine;
extern RFIDCoroutine rFIDCoroutine;
extern MainCoroutine mainCoroutine;
extern TimeEntryCoroutine timeEntryCoroutine;
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
        if (len > LogMessage_size) {
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
        if (request.responseHTTPcode() == 201     // Successfully created
            || request.responseHTTPcode() == 400  // Invalid file
            || request.responseHTTPcode() == 409  // Already uploaded
        ) {
          rFIDCoroutine.resetReader();  // needed to free SPI
          SD.remove(file.name());
          logsToUpload--;
        } else {
          COROUTINE_DELAY_SECONDS(300);
        }
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

void LogCoroutine::addProduct(int i) {
  if (logMessage.which__order == 0) {
    LogMessage_Order o = LogMessage_Order_init_zero;
    logMessage._order.order = o;
    logMessage.which__order = LogMessage_order_tag;
  }

  for (int j = 0; j < logMessage._order.order.cart_items_count; j++) {
    if (strcmp(logMessage._order.order.cart_items[j].product.name,
               configCoroutine.config.products[i].name) == 0) {
      logMessage._order.order.cart_items[i].amount++;
      return;
    }
  }

  // add to cart
  logMessage._order.order.cart_items[logMessage._order.order.cart_items_count]
      .amount = 1;
  logMessage._order.order.cart_items[logMessage._order.order.cart_items_count]
      .has_product = true;
  logMessage._order.order.cart_items[logMessage._order.order.cart_items_count]
      .product = configCoroutine.config.products[i];
  logMessage._order.order.cart_items_count++;
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

  if (logMessage.which__order != 0) {
    logMessage._order.order.payment_method = paymentMethod;
    if (configCoroutine.config.list_id > 0) {
      logMessage._order.order._list_id.list_id = configCoroutine.config.list_id;
      logMessage._order.order.which__list_id = LogMessage_Order_list_id_tag;
    }
  }

  if (paymentMethod == LogMessage_Order_PaymentMethod_KULT_CARD) {
    logMessage.which__card_transaction = LogMessage_card_transaction_tag;

    LogMessage_CardTransaction transaction =
        LogMessage_CardTransaction_init_zero;
    logMessage._card_transaction.card_transaction = transaction;

    switch (mainCoroutine.mode) {
      case CASH_OUT:
        logMessage._card_transaction.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_CASHOUT;
        break;
      case CHARGE_LIST:
      case CHARGE_MANUAL:
        logMessage._card_transaction.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_CHARGE;
        break;
      case TOP_UP:
        logMessage._card_transaction.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_TOP_UP;
        break;
      default: {
        // other events are not logged
        return;
      }
    }
    strncpy(logMessage._card_transaction.card_transaction.card_id,
            rFIDCoroutine.cardId,
            sizeof(logMessage._card_transaction.card_transaction.card_id));
    logMessage._card_transaction.card_transaction.balance_before =
        rFIDCoroutine.cardValueBefore.total;
    logMessage._card_transaction.card_transaction.deposit_before =
        rFIDCoroutine.cardValueBefore.deposit;
    logMessage._card_transaction.card_transaction.balance_after =
        rFIDCoroutine.cardValueAfter.total;
    logMessage._card_transaction.card_transaction.deposit_after =
        rFIDCoroutine.cardValueAfter.deposit;

    if (rFIDCoroutine.ultralightCounter != 0) {
      logMessage._card_transaction.card_transaction._counter.counter =
          rFIDCoroutine.ultralightCounter;
      logMessage._card_transaction.card_transaction.which__counter =
          LogMessage_CardTransaction_counter_tag;
    }
  }

  // write log file
  char filename[13];
  sprintf(filename, "%s.log", logMessage.client_id);

  rFIDCoroutine.resetReader();  // needed to free SPI
  File logFile = SD.open(filename, FILE_WRITE);
  if (logFile && logFile.availableForWrite()) {
    uint8_t buffer[LogMessage_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(logMessage));
    pb_encode(&stream, LogMessage_fields, &logMessage);
    logFile.write(buffer, stream.bytes_written);
    Log.infoln("[Log] Written logfile %s", filename);
    logsToUpload++;
  } else {
    Log.errorln("[Log] Could not create logfile %s", filename);
  }
}
