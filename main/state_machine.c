#include "state_machine.h"
#include <time.h>
#include "battery_test.h"
#include "buzzer.h"
#include "constants.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "local_config.h"
#include "log_writer.h"
#include "logmessage.pb.h"
#include "pb_encode.h"
#include "power_management.h"
#include "rfid.h"

#define BOOTSCREEN_DELAY_MS 1500

static const char* TAG = "state_machine";
static TimerHandle_t timeout_handle;
static QueueHandle_t state_events;

state_t current_state = {
    .mode = MAIN_STARTING_UP,
    .is_privileged = false,
    .manual_amount = 0,
    .menu_index = 0,
    .menu_index_active = -1,
    .product_selection =
        {
            .first_digit = -1,
            .second_digit = -1,
            .current_index = 0,
        },
    .cart = {
        .deposit = 0,
        .items = {},
        .item_count = 0,
    },
};

void trigger_event(event_t event) {
  xQueueSend(state_events, (void*)&(event_t){event}, portMAX_DELAY);
  // yield so the status will be updated before returning to the next instruction
  taskYIELD();
}

static void timeout_callback(TimerHandle_t timer) {
  trigger_event(TIMEOUT);
}

static void timeout(int ms) {
  if (timeout_handle == NULL) {
    timeout_handle = xTimerCreate("timeout_timer", pdMS_TO_TICKS(ms), pdFALSE, 0, timeout_callback);
    xTimerStart(timeout_handle, 0);
  } else {
    xTimerChangePeriod(timeout_handle, pdMS_TO_TICKS(ms), 0);
    xTimerReset(timeout_handle, 0);
  }
}

static mode_type default_mode() {
  current_state.menu_index_active = -1;
  return current_state.is_privileged ? PRIVILEGED_TOPUP : CHARGE_LIST;
}

static void reset_cart() {
  current_state.cart.deposit = 0;
  current_state.cart.item_count = 0;
  current_state.manual_amount = 0;
}

static void select_product(int product) {
  if (product >= active_config.products_count) {
    return;
  }
  if (current_state.cart.item_count >= 9) {
    return;
  }
  Product p = active_config.products[product];
  if (current_total() + p.price > MAX_BALANCE) {
    return;
  }

  for (int i = 0; i < current_state.cart.item_count; i++) {
    if (current_state.cart.items[i].has_product &&
        strcmp(current_state.cart.items[i].product.name, p.name) == 0) {
      current_state.cart.items[i].amount++;
      return;
    }
  }

  LogMessage_Order_CartItem item = {
      .amount = 1,
      .has_product = true,
      .product = p,
  };
  current_state.cart.items[current_state.cart.item_count] = item;
  current_state.cart.item_count++;
}

int current_total() {
  int total = current_state.cart.deposit * DEPOSIT_VALUE + current_state.manual_amount;
  for (int i = 0; i < current_state.cart.item_count; i++) {
    if (!current_state.cart.items[i].has_product) {
      continue;
    }
    total += current_state.cart.items[i].amount * current_state.cart.items[i].product.price;
  }
  return total;
}

static void update_deposit(bool up) {
  if (up && current_state.cart.deposit < 9 && current_total() + DEPOSIT_VALUE <= MAX_BALANCE) {
    current_state.cart.deposit++;
  } else if (!up && current_state.cart.deposit > -9) {
    current_state.cart.deposit--;
  }
}

static void remove_digit() {
  current_state.manual_amount /= 10;
}

static void add_digit(int d) {
  int add = current_state.manual_amount * 9 + d;
  if (current_total() + add > MAX_BALANCE) {
    return;
  }
  current_state.manual_amount += add;
}

static bool cart_is_empty() {
  return current_state.cart.item_count == 0 && current_state.cart.deposit == 0 &&
         current_state.manual_amount == 0;
}

static int is_leap_year(const struct tm* time) {
  uint16_t year = time->tm_year + 1900;  // Adjust for tm_year being years since 1900
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static uint16_t days_since_kult_epoch() {
  time_t now;
  time(&now);
  struct tm* current_time = gmtime(&now);
  static const uint8_t days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  uint16_t year_diff = current_time->tm_year - 125;  // 125 = 2025 - 1900

  // Calculate days from complete years
  uint16_t days = year_diff * 365;

  // Add leap days from complete years (2025 through last year)
  // We don't need to check year_diff > 0 since we know it's always true
  uint16_t complete_years = year_diff;
  days += complete_years / 4 - complete_years / 100 + complete_years / 400;

  // Current year's leap day (if applicable and we've passed February 29)
  if (is_leap_year(current_time) &&
      (current_time->tm_mon > 1 || (current_time->tm_mon == 1 && current_time->tm_mday == 29))) {
    days++;
  }

  // Add days in the current year
  uint16_t current_days = current_time->tm_mday - 1;  // -1 because we start from day 0
  for (uint8_t i = 0; i < current_time->tm_mon; i++) {
    current_days += days_per_month[i];

    // Add leap day if February in a leap year
    if (i == 1 && is_leap_year(current_time)) {
      current_days++;
    }
  }
  days += current_days;

  // Adjust for UTC reference time (UTC-04:00)
  if (current_time->tm_hour < 4) {
    days--;
  }

  return days;
}

static bool encode_crew_card_id(pb_ostream_t* stream, const pb_field_t* field, void* const* arg) {
  const uint8_t* card_id = (const uint8_t*)*arg;
  if (!pb_encode_tag_for_field(stream, field)) {
    return false;
  }
  return pb_encode_string(stream, card_id, LENGTH_ID);
}

static void log_crew_card_enrollment() {
  LogMessage* log = pvPortMalloc(sizeof(LogMessage));
  *log = (LogMessage)LogMessage_init_default;

  log->has_crew_card_enrollment = true;
  log->crew_card_enrollment.crew_card_id.size = LENGTH_ID;
  memcpy(log->crew_card_enrollment.crew_card_id.bytes, current_card.id, LENGTH_ID);
  log->crew_card_enrollment.valid_until = current_card.data.crew.valid_until;

  xQueueSendFromISR(log_queue, &log, NULL);
}

static void write_log(LogMessage_Order_PaymentMethod payment) {
  LogMessage* log = pvPortMalloc(sizeof(LogMessage));
  *log = (LogMessage)LogMessage_init_default;

  if (current_state.cart.item_count > 0) {
    log->has_order = true;
    log->order.payment_method = payment;
    log->order.has_list_id = true;
    log->order.list_id = active_config.list_id;
    log->order.cart_items_count = current_state.cart.item_count;
    for (int i = 0; i < current_state.cart.item_count; i++) {
      log->order.cart_items[i] = current_state.cart.items[i];
    }
  }
  if (payment == LogMessage_Order_PaymentMethod_KULT_CARD) {
    log->has_card_transaction = true;
    log->card_transaction.transaction_type = current_state.transaction_type;
    log->card_transaction.has_counter = true;
    log->card_transaction.counter = current_card.data.regular.counter;

    size_t length = sizeof(current_card.id);
    for (int i = 0; i < length; i++) {
      sprintf(log->card_transaction.card_id + i * 2, "%02X", current_card.id[i]);
    }
    log->card_transaction.card_id[length * 2] = '\0';

    log->card_transaction.balance_before = current_state.data_before_write.data.regular.balance;
    log->card_transaction.balance_after = current_state.data_to_write.data.regular.balance;
    log->card_transaction.deposit_before = current_state.data_before_write.data.regular.deposit;
    log->card_transaction.deposit_after = current_state.data_to_write.data.regular.deposit;
  } else if (payment == LogMessage_Order_PaymentMethod_FREE_CREW && current_card.type == CREW) {
    log->order.crew_card_id.funcs.encode = encode_crew_card_id;
    log->order.crew_card_id.arg = &current_card.id;
  }

  xQueueSendFromISR(log_queue, &log, NULL);
}

bool is_privileged_card() {
  for (int i = 0; i < MAX_PRIVILEGE_TOKENS; i++) {
    if (privilege_tokens[i].size == sizeof(current_card.id) &&
        memcmp(current_card.id, privilege_tokens[i].bytes, privilege_tokens[i].size) == 0) {
      return true;
    }
  }
  return false;
}

static mode_type crew_card_detected(event_t event) {
  for (int i = 0; i < MAX_SUSPENDED_CREW_CARDS; i++) {
    if (suspended_crew_cards[i].size == sizeof(current_card.id) &&
        memcmp(current_card.id, suspended_crew_cards[i].bytes, suspended_crew_cards[i].size) == 0) {
      current_state.card_error = CARD_SUSPENDED;
      trigger_beep(BEEP_LONG);
      return READ_FAILED;
    }
  }

  if (current_card.data.crew.valid_until < days_since_kult_epoch()) {
    current_state.card_error = CARD_EXPIRED;
    trigger_beep(BEEP_LONG);
    return READ_FAILED;
  }

  if (cart_is_empty()) {
    if (is_privileged_card()) {
      trigger_beep(BEEP_SHORT);
      current_state.is_privileged = !current_state.is_privileged;
      return default_mode();
    }
    return CREW_CARD_STATUS;
  }

  write_log(LogMessage_Order_PaymentMethod_FREE_CREW);
  reset_cart();
  trigger_beep(BEEP_SHORT);
  return default_mode();
}

static mode_type card_detected(event_t event) {
  if (event == CARD_DETECTED_NOT_READABLE) {
    current_state.card_error = TECHNICAL_ERROR;
    trigger_beep(BEEP_LONG);
    return READ_FAILED;
  } else if (event == CARD_DETECTED_SKIPPED_SECUIRTY) {
    current_state.card_error = INVALID_SIGNATURE;
    trigger_beep(BEEP_LONG);
    return READ_FAILED;
  } else if (event == CARD_DETECTED_OLD_CARD) {
    current_state.card_error = OLD_CARD;
    trigger_beep(BEEP_LONG);
    return READ_FAILED;
  } else if (event != CARD_DETECTED_OK) {
    // should not happen
    return MAIN_FATAL;
  }

  if (current_card.type == CREW) {
    return crew_card_detected(event);
  }

  if (cart_is_empty()) {
    trigger_beep(BEEP_SHORT);
    return CARD_BALANCE;
  }

  if (current_card.type != REGULAR) {
    return MAIN_FATAL;
  }

  // stroing in ints because it could go negative
  int new_balance = current_card.data.regular.balance;
  int new_deposit = current_card.data.regular.deposit;
  if (current_state.mode == CHARGE_LIST || current_state.mode == CHARGE_MANUAL) {
    new_balance -= current_total();
    new_deposit += current_state.cart.deposit;
  } else if (current_state.mode == PRIVILEGED_TOPUP) {
    new_balance += current_total();
    new_deposit -= current_state.cart.deposit;
  } else if (current_state.mode == PRIVILEGED_CASHOUT ||
             current_state.mode == PRIVILEGED_DONATION) {
    new_balance = 0;
    new_deposit = 0;
  } else if (current_state.mode == PRIVILEGED_REPAIR) {
    // no changes to balance/deposit, just rewrite current values
  } else {
    return current_state.mode;
  }

  if (new_balance < 0) {
    current_state.card_error = INSUFFICIENT_FUNDS;
    trigger_beep(BEEP_LONG);
    return WRITE_NOT_ATTEMPTED;
  }
  if (new_deposit < 0) {
    current_state.card_error = INSUFFICIENT_DEPOSIT;
    trigger_beep(BEEP_LONG);
    return WRITE_NOT_ATTEMPTED;
  }
  if (new_deposit > MAX_DEPOSIT) {
    current_state.card_error = CARD_LIMIT_EXCEEDED;
    trigger_beep(BEEP_LONG);
    return WRITE_NOT_ATTEMPTED;
  }
  if (new_balance + new_deposit * DEPOSIT_VALUE > MAX_BALANCE) {
    current_state.card_error = CARD_LIMIT_EXCEEDED;
    trigger_beep(BEEP_LONG);
    return WRITE_NOT_ATTEMPTED;
  }

  current_state.data_to_write = current_card;
  current_state.data_before_write = current_card;
  current_state.data_to_write.data.regular.balance = new_balance;
  current_state.data_to_write.data.regular.deposit = new_deposit;
  current_state.data_to_write.data.regular.counter = current_card.data.regular.counter + 1;

  return WRITE_CARD;
}

static mode_type product_list(event_t event) {
  static bool waiting_for_timeout = false;
  if (waiting_for_timeout && event != TIMEOUT) {
    return PRODUCT_LIST;
  }
  switch (event) {
    case KEY_A:
      if (current_state.product_selection.current_index > 0) {
        current_state.product_selection.current_index--;
      }
      break;
    case KEY_B:
      if (current_state.product_selection.current_index < active_config.products_count - 1) {
        current_state.product_selection.current_index++;
      }
      break;
    case KEY_0:
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
      if (current_state.product_selection.first_digit == -1 &&
          event - KEY_0 <= active_config.products_count / 10) {
        // set first digit
        current_state.product_selection.first_digit = event - KEY_0;
        current_state.product_selection.current_index =
            current_state.product_selection.first_digit * 10;
        if (current_state.product_selection.current_index > 0) {
          current_state.product_selection.current_index--;
        }
      } else if (current_state.product_selection.first_digit > -1 &&
                 current_state.product_selection.second_digit == -1) {
        int no = current_state.product_selection.first_digit * 10 + event - KEY_0;
        if (no > active_config.products_count || no < 1) {
          break;
        }
        // set second digit
        current_state.product_selection.second_digit = event - KEY_0;
        current_state.product_selection.current_index = no - 1;
        select_product(current_state.product_selection.current_index);
        waiting_for_timeout = true;
        timeout(400);
      }
      break;
    case KEY_D:
    case TIMEOUT:
      current_state.product_selection.first_digit = -1;
      current_state.product_selection.second_digit = -1;
      current_state.product_selection.current_index = 0;
      waiting_for_timeout = false;
      return CHARGE_LIST;
    case KEY_HASH:
      select_product(current_state.product_selection.current_index);
      uint8_t i = current_state.product_selection.current_index + 1;
      current_state.product_selection.first_digit = i / 10;
      current_state.product_selection.second_digit = i % 10;
      waiting_for_timeout = true;
      timeout(400);
      break;
    default:
      break;
  }
  return PRODUCT_LIST;
}

static mode_type charge_without_card(event_t event) {
  switch (event) {
    case KEY_1:
      write_log(LogMessage_Order_PaymentMethod_FREE_CREW);
      reset_cart();
      trigger_beep(BEEP_SHORT);
      return default_mode();
    case KEY_2:
      write_log(LogMessage_Order_PaymentMethod_CASH);
      reset_cart();
      trigger_beep(BEEP_SHORT);
      return default_mode();
    case KEY_3:
      write_log(LogMessage_Order_PaymentMethod_VOUCHER);
      reset_cart();
      trigger_beep(BEEP_SHORT);
      return default_mode();

    case KEY_STAR:
    case KEY_C:
    case KEY_D:
      return default_mode();

    default:
      break;
  }
  return CHARGE_WITHOUT_CARD;
}

static mode_type charge_list(event_t event) {
  switch (event) {
    case KEY_TRIPPLE_D:
      current_state.menu_index = 0;
      return MAIN_MENU;
    case KEY_STAR:
      return current_state.cart.item_count > 0 ? CHARGE_WITHOUT_CARD : CHARGE_MANUAL;
    case KEY_HASH:
      return PRODUCT_LIST;
    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);

    // stay in same state
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
      select_product(event - KEY_1);
      break;
    case KEY_A:
      update_deposit(true);
      break;
    case KEY_B:
      update_deposit(false);
      break;
    case KEY_C:
      if (current_state.cart.item_count > 0) {
        current_state.cart.items[current_state.cart.item_count - 1].has_product = false;
        current_state.cart.items[current_state.cart.item_count - 1].amount = 0;
        current_state.cart.item_count--;
      }
      break;
    case KEY_D:
      reset_cart();
      break;
    default:
      break;
  }
  return CHARGE_LIST;
}

static mode_type write_not_attemted(event_t event) {
  switch (event) {
    case CARD_REMOVED:
      return default_mode();
    default:
      break;
  }
  return WRITE_NOT_ATTEMPTED;
}

static mode_type write_failed(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_NOT_READABLE:
      if (memcmp(&current_card.id, &current_state.data_to_write.id, LENGTH_ID) == 0) {
        // same card detected, retry writing
        return WRITE_CARD;
      }
      break;
    case KEY_D:
      return default_mode();
    default:
      break;
  }
  return WRITE_FAILED;
}

static mode_type main_starting_up(event_t event) {
  switch (event) {
    case STARTUP_COMPLETED:
      return default_mode();

    // stay in same state
    default:
      break;
  }
  return MAIN_STARTING_UP;
}

static mode_type charge_manual(event_t event) {
  switch (event) {
    // change state
    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);

    // stay in same state
    case KEY_0:
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
      add_digit(event - KEY_0);
      break;
    case KEY_C:
      remove_digit();
      break;
    case KEY_A:
      update_deposit(true);
      break;
    case KEY_B:
      update_deposit(false);
      break;
    case KEY_D:
      if (cart_is_empty()) {
        return CHARGE_LIST;
      }
      reset_cart();
      break;
    case KEY_STAR:
    case KEY_HASH:
      reset_cart();
      return CHARGE_LIST;
    default:
      break;
  }
  return CHARGE_MANUAL;
}

static mode_type privileged_topup(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);

    // stay in same state
    case KEY_0:
      if (cart_is_empty()) {
        return PRIVILEGED_CASHOUT;
      }
    case KEY_1:
    case KEY_2:
    case KEY_3:
    case KEY_4:
    case KEY_5:
    case KEY_6:
    case KEY_7:
    case KEY_8:
    case KEY_9:
      add_digit(event - KEY_0);
      break;
    case KEY_A:
      update_deposit(true);
      break;
    case KEY_B:
      update_deposit(false);
      break;
    case KEY_C:
      remove_digit();
      break;
    case KEY_D:
      if (cart_is_empty()) {
        return PRIVILEGED_CASHOUT;
      }
      reset_cart();
      break;
    case KEY_TRIPPLE_D:
      current_state.menu_index = 0;
      return MAIN_MENU;
    case KEY_STAR:
      return PRIVILEGED_REPAIR;
    default:
      break;
  }
  return PRIVILEGED_TOPUP;
}

static mode_type main_product_lists(event_t event) {
  switch (event) {
    case KEY_A:
      if (current_state.menu_index > 0) {
        current_state.menu_index--;
      }
      break;
    case KEY_B:
      if (current_state.menu_index < lists_count - 1) {
        current_state.menu_index++;
      }
      break;
    case KEY_HASH:
      reset_cart();
      xQueueSendFromISR(config_update_queue, &product_lists[current_state.menu_index].id, NULL);
      timeout(400);
      break;
    case TIMEOUT:
      return default_mode();
    case KEY_D:
      current_state.menu_index = MENU_CONFIG;
      return MAIN_MENU;

    // stay in same state
    default:
      break;
  }
  return MAIN_PRODUCT_LISTS;
}

static mode_type write_card_initialize(event_t event) {
  switch (event) {
    case WRITE_SUCCESSFUL:
      trigger_beep(BEEP_SHORT);
      if (current_card.type == CREW) {
        log_crew_card_enrollment();
        return CREW_CARD_STATUS;
      }
      return CARD_BALANCE;
    case WRITE_UNSUCCESSFUL:
      trigger_beep(BEEP_LONG);
      current_state.card_error = TECHNICAL_ERROR;
      return WRITE_FAILED;
    default:
      break;
  }
  return WRITE_CARD_INITIALIZE;
}

static mode_type write_card(event_t event) {
  switch (event) {
    case WRITE_SUCCESSFUL:
      trigger_beep(BEEP_SHORT);
      write_log(LogMessage_Order_PaymentMethod_KULT_CARD);
      reset_cart();
      bool usb_connected = xEventGroupGetBits(event_group) & USB_CONNECTED;
      if (!usb_connected) {
        // if running on battery, exit privileged mode after transaction
        current_state.is_privileged = false;
      }
      return CARD_BALANCE;
    case WRITE_UNSUCCESSFUL:
      trigger_beep(BEEP_LONG);
      current_state.card_error = TECHNICAL_ERROR;
      return WRITE_FAILED;
    default:
      break;
  }
  return WRITE_CARD;
}

static mode_type card_balance(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
      return CARD_BALANCE;
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);
    case CARD_REMOVED:
      return default_mode();
    default:
      return CARD_BALANCE;
  }
}

static mode_type crew_card_status(event_t event) {
  switch (event) {
    case KEY_D:
      if (current_state.menu_index_active == MENU_INITIALIZE_CARD) {
        return INITIALIZE_CARD;
      }
      return default_mode();
    default:
      return CREW_CARD_STATUS;
  }
}

static mode_type privileged_cashout_or_donation(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
      current_state.data_to_write = current_card;
      current_state.data_before_write = current_card;
      current_state.data_to_write.data.regular.balance = 0;
      current_state.data_to_write.data.regular.deposit = 0;
      current_state.data_to_write.data.regular.counter = current_card.data.regular.counter + 1;
      return WRITE_CARD;
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);
    case KEY_A:
    case KEY_B:
      return current_state.mode == PRIVILEGED_CASHOUT ? PRIVILEGED_DONATION : PRIVILEGED_CASHOUT;
    case KEY_D:
    case TIMEOUT:
      return default_mode();
    default:
      return current_state.mode;
  }
}

static mode_type privileged_repair(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
      current_state.data_to_write = current_card;
      current_state.data_before_write = current_card;
      current_state.data_to_write.data.regular.counter = current_card.data.regular.counter + 1;
      return WRITE_CARD;
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);
    case KEY_D:
      return default_mode();
    default:
      return PRIVILEGED_REPAIR;
  }
}

static mode_type initialize_card(event_t event) {
  switch (event) {
    case CARD_DETECTED_UNINITIALIZED:
    case CARD_DETECTED_OK:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
      uint16_t valid_until = current_state.data_to_write.data.crew.valid_until;
      current_state.data_to_write = current_card;
      if (event == CARD_DETECTED_UNINITIALIZED) {
        current_state.data_to_write.type = current_state.is_privileged ? CREW : REGULAR;
      }
      if (current_state.data_to_write.type == CREW) {
        current_state.data_to_write.data.crew.valid_until = valid_until;
      } else if (current_state.data_to_write.type == REGULAR) {
        current_state.data_to_write.data.regular.balance = 0;
        current_state.data_to_write.data.regular.deposit = 0;
      } else {
        return MAIN_FATAL;
      }

      return WRITE_CARD_INITIALIZE;
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);
    case KEY_A:
      current_state.data_to_write.data.crew.valid_until++;
      break;
    case KEY_B:
      if (current_state.data_to_write.data.crew.valid_until > 0) {
        current_state.data_to_write.data.crew.valid_until--;
      }
      break;
    case KEY_D:
      return default_mode();
    default:
      break;
  }
  return INITIALIZE_CARD;
}

static mode_type read_failed(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
    case CARD_DETECTED_OLD_CARD:
      return card_detected(event);
    case CARD_REMOVED:
      return default_mode();
    default:
      return READ_FAILED;
  }
}

static mode_type main_menu(event_t event) {
  switch (event) {
    case KEY_A:
      if (current_state.menu_index > 0) {
        current_state.menu_index--;
      }
      break;
    case KEY_B:
      if (current_state.menu_index < MENU_COUNT - 1) {
        current_state.menu_index++;
      }
      break;
    case KEY_HASH:
      switch (current_state.menu_index) {
        case MENU_CONFIG:
          for (int i = 0; i < lists_count; i++) {
            if (product_lists[i].id == active_config.list_id) {
              current_state.menu_index = i;
              break;
            }
          }
          if (lists_count > 0) {
            return MAIN_PRODUCT_LISTS;
          }
          break;
        case MENU_SOUND:
          toggle_sound_mode();
          timeout(400);
          break;
        case MENU_BATTERY_TEST:
          xTaskCreate(&battery_test, BATTERY_TEST_TASK, 4096, NULL, TASK_PRIO_NORMAL, NULL);
          return BATTERY_TEST;
        case MENU_BUZZER_TEST:
          if (current_state.submenu_index < _BEEP_COUNT - 1) {
            current_state.submenu_index++;
          } else {
            current_state.submenu_index = 0;
          }
          trigger_forced_beep(current_state.submenu_index);
          break;
        case MENU_UPDATE:
          current_state.menu_index_active = MENU_UPDATE;
          timeout(400);
          vTaskNotifyGiveFromISR(xTaskGetHandle(FETCH_CONFIG_TASK), NULL);
          break;
        case MENU_WIFI:
          current_state.menu_index_active = MENU_WIFI;
          timeout(400);
          vTaskNotifyGiveFromISR(xTaskGetHandle(WIFI_CONNECT_TASK), NULL);
          break;
        case MENU_UPLOADS:
          current_state.menu_index_active = MENU_UPLOADS;
          xTaskNotify(xTaskGetHandle(LOG_UPLOADER_TASK), 0, eNoAction);
          timeout(400);
          break;
        case MENU_INITIALIZE_CARD:
          current_state.menu_index_active = MENU_INITIALIZE_CARD;
          if (current_state.is_privileged) {
            current_state.data_to_write.type = CREW;
            static uint16_t valid_until = 0;
            if (valid_until == 0) {
              valid_until = days_since_kult_epoch() + 1;
            }
            current_state.data_to_write.data.crew.valid_until = valid_until;
          } else {
            current_state.data_to_write.type = REGULAR;
          }
          timeout(400);
          return INITIALIZE_CARD;

        default:
          break;
      }
      break;
    case KEY_D:
      return default_mode();
    case TIMEOUT:
      current_state.menu_index_active = -1;
      break;
    default:
      break;
  }
  return MAIN_MENU;
}

static mode_type process_event(event_t event) {
  if (event == FATAL_ERROR) {
    return MAIN_MENU;
  } else if (event == ENTER_POWER_SAVE) {
    return POWER_SAVE;
  }

  if (event == KEY_0 || event == KEY_1 || event == KEY_2 || event == KEY_3 || event == KEY_4 ||
      event == KEY_5 || event == KEY_6 || event == KEY_7 || event == KEY_8 || event == KEY_9 ||
      event == KEY_A || event == KEY_B || event == KEY_C || event == KEY_D || event == KEY_STAR ||
      event == KEY_HASH) {
    reset_power_off_timer();
  }

  switch (current_state.mode) {
    case CHARGE_LIST:
      return charge_list(event);
    case CHARGE_MANUAL:
      return charge_manual(event);
    case PRODUCT_LIST:
      return product_list(event);
    case CHARGE_WITHOUT_CARD:
      return charge_without_card(event);
    case PRIVILEGED_TOPUP:
      return privileged_topup(event);
    case PRIVILEGED_CASHOUT:
    case PRIVILEGED_DONATION:
      return privileged_cashout_or_donation(event);
    case PRIVILEGED_REPAIR:
      return privileged_repair(event);
    case INITIALIZE_CARD:
      return initialize_card(event);
    case WRITE_CARD:
      return write_card(event);
    case WRITE_CARD_INITIALIZE:
      return write_card_initialize(event);
    case WRITE_FAILED:
      return write_failed(event);
      break;
    case CARD_BALANCE:
      return card_balance(event);
      break;
    case CREW_CARD_STATUS:
      return crew_card_status(event);
      break;
    case READ_FAILED:
      return read_failed(event);
      break;
    case WRITE_NOT_ATTEMPTED:
      return write_not_attemted(event);
      break;
    case MAIN_STARTING_UP:
      return main_starting_up(event);
    case MAIN_MENU:
      return main_menu(event);
    case MAIN_PRODUCT_LISTS:
      return main_product_lists(event);
    case MAIN_FATAL:
    case BATTERY_TEST:
    case POWER_SAVE:
      // cannot leave these states
      break;
  }
  return current_state.mode;
}

void state_machine(void* params) {
  state_events = xQueueCreate(5, sizeof(int));
  portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
  current_state.expected_bootup_time = esp_timer_get_time() + BOOTSCREEN_DELAY_MS * 1000;

  ESP_LOGI(TAG, "waiting for startup to complete");

  while (true) {
    xEventGroupWaitBits(
        event_group, STARTUP_BITS, pdFALSE, pdFALSE, BOOTSCREEN_DELAY_MS / portTICK_PERIOD_MS
    );
    if (xEventGroupGetBits(event_group) & STARTUP_BITS) {
      vTaskDelay(
          (current_state.expected_bootup_time - esp_timer_get_time()) / 1000 / portTICK_PERIOD_MS
      );
      break;
    } else {
      current_state.mode = MAIN_MENU;
      trigger_event(DISPLAY_NEEDS_UPDATE);
    }
  }
  trigger_event(STARTUP_COMPLETED);
  trigger_beep(STARTUP);

  event_t event;
  while (true) {
    xQueueReceive(state_events, &event, portMAX_DELAY);

    // state manipulation should not be interrupted, to prevent inconsistent state
    taskENTER_CRITICAL(&mutex);
    mode_type previous_mode = current_state.mode;
    current_state.mode = process_event(event);
    // state entry events
    switch (current_state.mode) {
      case CHARGE_LIST:
      case CHARGE_MANUAL:
        current_state.transaction_type = LogMessage_CardTransaction_TransactionType_CHARGE;
        break;
      case PRIVILEGED_TOPUP:
        current_state.transaction_type = LogMessage_CardTransaction_TransactionType_TOP_UP;
        break;
      case PRIVILEGED_CASHOUT:
        current_state.transaction_type = LogMessage_CardTransaction_TransactionType_CASHOUT;
        break;
      case PRIVILEGED_DONATION:
        current_state.transaction_type = LogMessage_CardTransaction_TransactionType_DONATION;
        break;
      case PRIVILEGED_REPAIR:
        current_state.transaction_type = LogMessage_CardTransaction_TransactionType_REPAIR;
        break;
      default:
        break;
    }
    taskEXIT_CRITICAL(&mutex);

    if (previous_mode != current_state.mode) {
      // log needs to be outside of critical section
      ESP_LOGI(
          TAG, "Event %d changed state from %d to %d", event, previous_mode, current_state.mode
      );
    }
    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  }
}
