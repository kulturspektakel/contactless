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
        if (request.responseHTTPcode() == 201     // Successfully created
            || request.responseHTTPcode() == 400  // Invalid file
            || request.responseHTTPcode() == 409  // Already uploaded
        ) {
          rFIDCoroutine.resetReader();  // needed to free SPI
          SD.remove(file.name());
          logsToUpload--;
        } else {
          // TODO: How to handle other status codes?
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
  for (int j = 0; j < transaction.cart_items_count; j++) {
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

void LogCoroutine::writeLog(CardTransaction_PaymentMethod paymentMethod) {
  switch (mainCoroutine.mode) {
    case CASH_OUT:
      transaction.transaction_type = CardTransaction_TransactionType_CASHOUT;
      break;
    case CHARGE_LIST:
    case CHARGE_MANUAL:
      transaction.transaction_type = CardTransaction_TransactionType_CHARGE;
      break;
    case TOP_UP:
      transaction.transaction_type = CardTransaction_TransactionType_TOP_UP;
      break;
    default: {
      // other events are not logged
      CardTransaction t = CardTransaction_init_zero;
      transaction = t;
      return;
    }
  }
  transaction.device_time = now();
  transaction.device_time_is_utc = timeEntryCoroutine.deviceTimeIsUtc;
  transaction.payment_method = paymentMethod;
  strncpy(transaction.card_id, rFIDCoroutine.cardId,
          sizeof(transaction.card_id));
  transaction.balance_before = rFIDCoroutine.cardValueBefore.total;
  transaction.deposit_before = rFIDCoroutine.cardValueBefore.deposit;
  transaction.balance_after = rFIDCoroutine.cardValueAfter.total;
  transaction.deposit_after = rFIDCoroutine.cardValueAfter.deposit;
  if (configCoroutine.config.list_id > 0) {
    transaction._list_id.list_id = configCoroutine.config.list_id;
    transaction.which__list_id = CardTransaction_list_id_tag;
  }

  strncpy(transaction.device_id, deviceID, 9);

  // generate client transaction ID
  for (size_t i = 0; i < sizeof(transaction.client_id) - 1; i++) {
    // TODO Random not random
    transaction.client_id[i] = charset[rand() % (int)(sizeof(charset) - 1)];
  }
  transaction.client_id[sizeof(transaction.client_id) - 1] = '\0';
  if (rFIDCoroutine.ultralightCounter != 0) {
    transaction._counter.counter = rFIDCoroutine.ultralightCounter;
    transaction.which__counter = CardTransaction_counter_tag;
  }

  // write log file
  char filename[13];
  sprintf(filename, "%s.log", transaction.client_id);

  rFIDCoroutine.resetReader();  // needed to free SPI
  File logFile = SD.open(filename, FILE_WRITE);
  if (logFile && logFile.availableForWrite()) {
    uint8_t buffer[CardTransaction_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(transaction));
    pb_encode(&stream, CardTransaction_fields, &transaction);
    logFile.write(buffer, stream.bytes_written);
    Log.infoln("[Log] Written logfile %s", filename);
    logsToUpload++;
  } else {
    Log.errorln("[Log] Could not create logfile %s", filename);
  }
}
