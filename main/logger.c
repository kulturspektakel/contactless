#include "logger.h"
#include <dirent.h>
#include <time.h>
#include "device_id.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nanopb/pb_encode.h"
#include "power_management.h"
#include "state_machine.h"

static const char* TAG = "logger";
static const char* ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char* LOG_DIR = "/littlefs/logs";

bool write_pb_to_file(pb_ostream_t* stream, const uint8_t* buffer, size_t count) {
  FILE* file = (FILE*)stream->state;
  int ret = fwrite(buffer, count, 1, file);
  if (ret < 0) {
    return false;
  } else if (ret == 0) {
    stream->bytes_written = 0;
    return false;
  }
  return true;
}

void write_log(LogMessage_Order_PaymentMethod payment_method) {
  LogMessage log_message = LogMessage_init_default;

  // Card details
  if (payment_method == LogMessage_Order_PaymentMethod_KULT_CARD) {
    log_message.has_card_transaction = true;
    LogMessage_CardTransaction transaction = LogMessage_CardTransaction_init_zero;
    log_message.card_transaction = transaction;
    switch (current_state.mode) {
      case CHARGE_LIST:
      case CHARGE_MANUAL:
        log_message.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_CHARGE;
        break;
      case PRIVILEGED_CASHOUT:
        log_message.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_CASHOUT;
        break;
      case PRIVILEGED_TOPUP:
        log_message.card_transaction.transaction_type =
            LogMessage_CardTransaction_TransactionType_TOP_UP;
        break;
      default: {
        // other events are not logged
        return;
      }
    }

    // TODO
    log_message.card_transaction.has_counter = true;
    log_message.card_transaction.counter = 0;
    strncpy(log_message.card_transaction.card_id, "", sizeof(log_message.card_transaction.card_id));
    log_message.card_transaction.balance_after = 0;
    log_message.card_transaction.balance_before = 0;
    log_message.card_transaction.deposit_after = 0;
    log_message.card_transaction.deposit_before = 0;
  }

  // Order details
  if (current_state.cart.item_count > 0) {
    log_message.has_order = true;
    log_message.order.payment_method = payment_method;
    log_message.order.cart_items_count = current_state.cart.item_count;
    for (int i = 0; i < current_state.cart.item_count; i++) {
      log_message.order.cart_items[i] = current_state.cart.items[i];
    }
  }

  // Device details
  for (int i = 0; i < sizeof(log_message.client_id) - 1; i++) {
    log_message.client_id[i] = ALPHABET[esp_random() % strlen(ALPHABET)];
  }
  log_message.client_id[sizeof(log_message.client_id) - 1] = '\0';
  log_message.device_time = time(NULL);
  log_message.device_time_is_utc = true;
  log_message.has_battery_voltage = true;
  log_message.battery_voltage = battery_voltage;
  log_message.has_usb_voltage = true;
  log_message.usb_voltage = usb_voltage;
  device_id(log_message.device_id);

  char filename[29];
  sprintf(filename, "%s/%.8s.log", LOG_DIR, log_message.client_id);
  ESP_LOGI(TAG, "Write log file %s", filename);

  FILE* log_file = fopen(filename, "w");
  if (log_file != NULL) {
    pb_ostream_t file_stream = {
        .callback = write_pb_to_file,
        .state = log_file,
        .max_size = SIZE_MAX,
        .bytes_written = 0,
    };

    if (pb_encode(&file_stream, LogMessage_fields, &log_message)) {
      ESP_LOGI(TAG, "Wrote log to %s", filename);
      current_state.log_files_to_upload++;
      xTaskNotifyGive(xTaskGetHandle("log_uploader"));
      LogMessage l = LogMessage_init_default;
      log_message = l;
    } else {
      ESP_LOGE(TAG, "Failed to write log to %s", filename);
    }

    fclose(log_file);
  } else {
    ESP_LOGE(TAG, "Failed to open %s for writing", filename);
  }
}
