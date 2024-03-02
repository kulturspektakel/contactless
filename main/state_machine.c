#include "state_machine.h"
#include "buzzer.h"
#include "constants.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_group.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "local_config.h"
#include "log_writer.h"
#include "logmessage.pb.h"
#include "rfid.h"

#define BOOTSCREEN_DELAY_MS 2000

static const char* TAG = "state_machine";
static TimerHandle_t timeout_handle;
static QueueHandle_t state_events;

state_t current_state = {
    .mode = MAIN_STARTING_UP,
    .is_privileged = false,
    .main_menu = {.count = 0},
    .log_files_to_upload = -1,
    .manual_amount = 0,
    .product_selection =
        {
            .first_digit = -1,
            .second_digit = -1,
            .current_index = 0,
        },
    .cart =
        {
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
  if (current_total() + p.price > 9999) {
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
  if (up && current_state.cart.deposit < 9 && current_total() + DEPOSIT_VALUE <= 9999) {
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
  if (current_total() + add > 9999) {
    return;
  }
  current_state.manual_amount += add;
}

static mode_type token_detected(event_t event) {
  trigger_beep(BEEP_SHORT);
  reset_cart();
  current_state.is_privileged = !current_state.is_privileged;
  return default_mode();
}

static bool cart_is_empty() {
  return current_state.cart.item_count == 0 && current_state.cart.deposit == 0 &&
         current_state.manual_amount == 0;
}

static mode_type card_detected(event_t event) {
  if (event == CARD_DETECTED_NOT_READABLE) {
    trigger_beep(BEEP_LONG);
    return CARD_WITH_PROBLEM;
  } else if (event == CARD_DETECTED_SKIPPED_SECUIRTY) {
    trigger_beep(BEEP_LONG);
    return CARD_WITH_PROBLEM;
  } else if (event != CARD_DETECTED_OK) {
    // should not happen
    // TODO: fatal error
    return current_state.mode;
  }

  if (cart_is_empty()) {
    timeout(2000);
    trigger_beep(BEEP_SHORT);
    return CARD_BALANCE;
  }

  // stroing in ints because it could go negative
  int new_balance = current_card.balance;
  int new_deposit = current_card.deposit;
  int new_counter = current_card.counter;
  if (current_state.mode == CHARGE_LIST || current_state.mode == CHARGE_MANUAL) {
    new_balance -= current_total();
    new_deposit += current_state.cart.deposit;
    new_counter++;
  } else if (current_state.mode == PRIVILEGED_TOPUP) {
    new_balance += current_total();
    new_deposit += current_state.cart.deposit;
    new_counter++;
  } else if (current_state.mode == PRIVILEGED_CASHOUT) {
    new_balance = 0;
    new_deposit = 0;
    new_counter++;
  } else if (current_state.mode == PRIVILEGED_REPAIR) {
    // no changes, just rewrite current values
  } else {
    return current_state.mode;
  }

  if (new_balance < 0) {
    current_state.write_failed_reason = INSUFFICIENT_FUNDS;
    trigger_beep(BEEP_LONG);
    return WRITE_FAILED;
  }
  if (new_deposit < 0) {
    current_state.write_failed_reason = INSUFFICIENT_DEPOSIT;
    trigger_beep(BEEP_LONG);
    return WRITE_FAILED;
  }
  if (new_deposit > 9) {
    current_state.write_failed_reason = CARD_LIMIT_EXCEEDED;
    trigger_beep(BEEP_LONG);
    return WRITE_FAILED;
  }
  if (new_balance + new_deposit * DEPOSIT_VALUE > 9999) {
    current_state.write_failed_reason = CARD_LIMIT_EXCEEDED;
    trigger_beep(BEEP_LONG);
    return WRITE_FAILED;
  }

  current_state.data_to_write = current_card;
  current_state.data_before_write = current_card;
  current_state.data_to_write.balance = new_balance;
  current_state.data_to_write.deposit = new_deposit;
  current_state.data_to_write.counter = new_counter;

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
      } else if (current_state.product_selection.first_digit > -1 && current_state.product_selection.second_digit == -1) {
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

static void write_log(LogMessage_Order_PaymentMethod payment) {
  LogMessage* log = pvPortMalloc(sizeof(LogMessage) LogMessage_init_default);

  if (current_state.cart.item_count > 0) {
    log->has_order = true;
    log->order.payment_method = payment;
    log->order.cart_items_count = current_state.cart.item_count;
    for (int i = 0; i < current_state.cart.item_count; i++) {
      log->order.cart_items[i] = current_state.cart.items[i];
    }
  }

  if (payment == LogMessage_Order_PaymentMethod_KULT_CARD) {
    log->has_card_transaction = true;
    log->card_transaction.transaction_type = current_state.transaction_type;
    log->card_transaction.has_counter = true;
    log->card_transaction.counter = current_card.counter;

    for (int i = 0; i < sizeof(current_card.id); i++) {
      char buffer[3];
      snprintf(buffer, sizeof(buffer), "%02X", current_card.id[i]);
      log->card_transaction.card_id[i * 2] = buffer[0];
      log->card_transaction.card_id[i * 2 + 1] = buffer[1];
    }

    log->card_transaction.balance_before = current_state.data_before_write.balance;
    log->card_transaction.balance_after = current_state.data_to_write.balance;
    log->card_transaction.deposit_before = current_state.data_before_write.deposit;
    log->card_transaction.deposit_after = current_state.data_to_write.deposit;
  }

  xQueueSend(log_queue, &log, portMAX_DELAY);
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
      current_state.main_menu = initialize_main_menu();
      return MAIN_MENU;
    case KEY_STAR:
      return current_state.cart.item_count > 0 ? CHARGE_WITHOUT_CARD : CHARGE_MANUAL;
    case KEY_HASH:
      return PRODUCT_LIST;
    case PRIVILEGE_TOKEN_DETECTED:
      return token_detected(event);
    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
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
    case KEY_D:
      reset_cart();
      break;
    default:
      break;
  }
  return CHARGE_LIST;
}

static mode_type write_failed(event_t event) {
  switch (event) {
    case CARD_DETECTED_SKIPPED_SECUIRTY:
      // shouldn't happen, because security is always skipped in WRITE_FAILED mode
    case CARD_DETECTED_OK:
      if (memcmp(&current_card.id, &current_state.data_to_write.id, LENGTH_ID) == 0) {
        return WRITE_CARD;
      }
      break;
    case CARD_DETECTED_NOT_READABLE:
      return card_detected(event);
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
    case PRIVILEGE_TOKEN_DETECTED:
      return token_detected(event);

    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
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
    case PRIVILEGE_TOKEN_DETECTED:
      return token_detected(event);

    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
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
    case KEY_A:
      update_deposit(false);
      break;
    case KEY_B:
      update_deposit(true);
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
    default:
      break;
  }
  return PRIVILEGED_TOPUP;
}

static mode_type main_menu(event_t event) {
  switch (event) {
    case KEY_A:
      if (current_state.main_menu.active_item > 0) {
        current_state.main_menu.active_item--;
      }
      break;
    case KEY_B:
      if (current_state.main_menu.active_item < current_state.main_menu.count - 1) {
        current_state.main_menu.active_item++;
      }
      break;
    case KEY_HASH:
      reset_cart();
      select_list(current_state.main_menu.items[current_state.main_menu.active_item].list_id);
      timeout(400);
      break;
    case KEY_D:
    case TIMEOUT:
      current_state.main_menu.count = 0;
      vPortFree(current_state.main_menu.items);
      return default_mode();

    // stay in same state
    default:
      break;
  }
  return MAIN_MENU;
}

static mode_type write_card(event_t event) {
  switch (event) {
    case WRITE_SUCCESSFUL:
      trigger_beep(BEEP_SHORT);
      write_log(LogMessage_Order_PaymentMethod_KULT_CARD);
      reset_cart();
      timeout(1500);
      return CARD_BALANCE;
    case WRITE_UNSUCCESSFUL:
      trigger_beep(BEEP_LONG);
      current_state.write_failed_reason = TECHNICAL_ERROR;
      return WRITE_FAILED;
    default:
      break;
  }
  return WRITE_CARD;
}

static mode_type card_balance(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
      timeout(1500);
      return CARD_BALANCE;
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
      return card_detected(event);
    case TIMEOUT:
      return default_mode();
    default:
      return default_mode();
  }
}

static mode_type privileged_cashout(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
      return WRITE_CARD;
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
      return card_detected(event);
    case KEY_D:
    case TIMEOUT:
      return default_mode();
    default:
      return PRIVILEGED_CASHOUT;
  }
}

static mode_type card_with_problem(event_t event) {
  switch (event) {
    case CARD_DETECTED_OK:
    case CARD_DETECTED_NOT_READABLE:
    case CARD_DETECTED_SKIPPED_SECUIRTY:
      return card_detected(event);
    case TIMEOUT:
      return default_mode();
  }
}

static mode_type process_event(event_t event) {
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
      return privileged_cashout(event);
    case PRIVILEGED_REPAIR:
      break;

    case WRITE_CARD:
      return write_card(event);
    case WRITE_FAILED:
      return write_failed(event);
      break;

    case CARD_BALANCE:
      return card_balance(event);
      break;

    case CARD_WITH_PROBLEM:
      return card_with_problem(event);
      break;

    case MAIN_MENU:
      return main_menu(event);
    case MAIN_STARTING_UP:
      return main_starting_up(event);
    case MAIN_FATAL:
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
        event_group, startup_bits, pdFALSE, pdFALSE, BOOTSCREEN_DELAY_MS / portTICK_PERIOD_MS
    );
    if (xEventGroupGetBits(event_group) & startup_bits) {
      // startup completed, wait at least 2 seconds
      vTaskDelay(
          (current_state.expected_bootup_time - esp_timer_get_time()) / 1000 / portTICK_PERIOD_MS
      );
      break;
    }
    trigger_event(DISPLAY_NEEDS_UPDATE);
  }
  trigger_event(STARTUP_COMPLETED);

  event_t event;
  while (true) {
    xQueueReceive(state_events, &event, portMAX_DELAY);
    mode_type previous_mode = current_state.mode;

    // state manipulation should not be interrupted, to prevent inconsistent state
    taskENTER_CRITICAL(&mutex);
    // state exit events

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
    // taskEXIT_CRITICAL(&mutex);
    xEventGroupSetBits(event_group, DISPLAY_NEEDS_UPDATE);
  }
}
