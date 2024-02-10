#include "state_machine.h"
#include "esp_log.h"
#include "event_group.h"
#include "local_config.h"
#include "logger.h"
#include "logmessage.pb.h"
#include "rfid.h"

static const char* TAG = "state_machine";
static TimerHandle_t timeout_handle;

QueueHandle_t state_events;
state_t current_state = {
    .mode = MAIN_STARTING_UP,
    .previous_mode = MAIN_STARTING_UP,
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

static void timeout_callback(TimerHandle_t timer) {
  xQueueSend(state_events, (void*)&(event_t){TIMEOUT}, portMAX_DELAY);
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
  int total = current_state.cart.deposit * 200 + current_state.manual_amount;
  for (int i = 0; i < current_state.cart.item_count; i++) {
    if (!current_state.cart.items[i].has_product) {
      continue;
    }
    total += current_state.cart.items[i].amount * current_state.cart.items[i].product.price;
  }
  return total;
}

static void update_deposit(bool up) {
  if (up && current_state.cart.deposit < 9 && current_total() + 200 <= 9999) {
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
  reset_cart();
  current_state.is_privileged = true;
  return PRIVILEGED_TOPUP;
}

static mode_type card_detected(event_t event) {
  if (current_state.cart.item_count == 0 && current_state.cart.deposit == 0 &&
      current_state.manual_amount == 0) {
    timeout(2000);
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
    new_deposit -= current_state.cart.deposit;
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
    return WRITE_FAILED;
  }
  if (new_deposit < 0) {
    current_state.write_failed_reason = INSUFFICIENT_DEPOSIT;
    return WRITE_FAILED;
  }
  if (new_deposit > 9) {
    current_state.write_failed_reason = CARD_LIMIT_EXCEEDED;
    return WRITE_FAILED;
  }
  if (new_balance + new_deposit * 200 > 9999) {
    current_state.write_failed_reason = CARD_LIMIT_EXCEEDED;
    return WRITE_FAILED;
  }

  current_state.data_to_write = current_card;
  current_state.data_to_write.balance = new_balance;
  current_state.data_to_write.deposit = new_deposit;
  current_state.data_to_write.counter = new_counter;

  return WRITE_CARD;
}

static mode_type product_list(event_t event) {
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
        timeout(400);
      }
      break;
    case KEY_D:
    case TIMEOUT:
      current_state.product_selection.first_digit = -1;
      current_state.product_selection.second_digit = -1;
      current_state.product_selection.current_index = 0;
      return CHARGE_LIST;
    case KEY_HASH:
      select_product(current_state.product_selection.current_index);
      uint8_t i = current_state.product_selection.current_index + 1;
      current_state.product_selection.first_digit = i / 10;
      current_state.product_selection.second_digit = i % 10;
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
      return default_mode();
    case KEY_2:
      write_log(LogMessage_Order_PaymentMethod_CASH);
      reset_cart();
      return default_mode();
    case KEY_3:
      write_log(LogMessage_Order_PaymentMethod_VOUCHER);
      reset_cart();
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
    case TOKEN_DETECTED:
      return token_detected(event);
    case CARD_DETECTED:
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
    case CARD_DETECTED:
      if (memcmp(&current_card.id, &current_state.data_to_write.id, LENGTH_ID) == 0) {
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
    case TOKEN_DETECTED:
      return token_detected(event);
    case CARD_DETECTED:
      return card_detected(event);
    case KEY_STAR:
      return current_state.cart.item_count > 0 ? CHARGE_WITHOUT_CARD : CHARGE_MANUAL;

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
      reset_cart();
      break;
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
    case TOKEN_DETECTED:
      reset_cart();
      current_state.is_privileged = false;
      return CHARGE_LIST;
    case CARD_DETECTED:
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
      update_deposit(true);
      break;
    case KEY_B:
      update_deposit(false);
      break;
    case KEY_C:
      remove_digit();
      break;
    case KEY_D:
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
      free(current_state.main_menu.items);
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
      reset_cart();
      timeout(1500);
      return CARD_BALANCE;
    case WRITE_UNSUCCESSFUL:
      current_state.write_failed_reason = TECHNICAL_ERROR;
      return WRITE_FAILED;
    default:
      break;
  }
  return WRITE_CARD;
}

static mode_type card_balance(event_t event) {
  switch (event) {
    case CARD_DETECTED:
      timeout(1500);
      return CARD_BALANCE;
    case TIMEOUT:
      return default_mode();
    default:
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

  ESP_LOGI(TAG, "waiting for startup to complete");

  xEventGroupWaitBits(event_group, LOCAL_CONFIG_LOADED | TIME_SET, pdFALSE, pdTRUE, portMAX_DELAY);
  xQueueSend(state_events, (void*)&(event_t){STARTUP_COMPLETED}, portMAX_DELAY);

  event_t event;
  while (true) {
    xQueueReceive(state_events, &event, portMAX_DELAY);
    // state manipulation should not be interrupted, to prevent inconsistent state
    taskENTER_CRITICAL(&mutex);
    mode_type previous_mode = current_state.mode;
    current_state.mode = process_event(event);
    if (previous_mode != current_state.mode) {
      current_state.previous_mode = previous_mode;
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
