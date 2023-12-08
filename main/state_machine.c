#include "state_machine.h"
#include "esp_log.h"
#include "event_group.h"
#include "local_config.h"

static const char* TAG = "state_machine";

QueueHandle_t state_events;
state_t current_state = {
    .mode = MAIN_STARTING_UP,
    .is_privileged = false,
    .first_digit = -1,
    .cart =
        {
            .deposit = 0,
            .total = 0,
            .products = {},
            .product_count = 0,
        },
};

mode_type default_mode() {
  return current_state.is_privileged ? PRIVILEGED_TOPUP : CHARGE_LIST;
}

void reset_cart() {
  current_state.cart.deposit = 0;
  current_state.cart.total = 0;
  current_state.cart.product_count = 0;
}

void select_product(int product) {
  if (active_config.products_count > product) {
    return;
  }
  if (current_state.cart.product_count >= 9) {
    return;
  }
  Product p = active_config.products[product - 1];
  if (current_state.cart.total + p.price > 9999) {
    return;
  }
  current_state.cart.total += p.price;
  current_state.cart.products[current_state.cart.product_count] = p;
  current_state.cart.product_count++;
}

void update_deposit(bool up) {
  if (up && current_state.cart.deposit < 9) {
    current_state.cart.deposit++;
  } else if (!up && current_state.cart.deposit > -9) {
    current_state.cart.deposit--;
  }
}

void remove_digit() {
  current_state.cart.deposit /= 10;
}

void add_digit(int d) {
  if (current_state.cart.total < 1000) {
    current_state.cart.deposit = current_state.cart.deposit * 10 + d;
  }
}

mode_type two_digit_number_pressed(int d) {
  if (current_state.first_digit == -1) {
    current_state.first_digit = d;
    return CHARGE_LIST_TWO_DIGIT;
  }
  select_product(10 * current_state.first_digit + d);
  current_state.first_digit = -1;
  return CHARGE_LIST;
}

mode_type token_detected(event_t event) {
  reset_cart();
  current_state.is_privileged = true;
  return PRIVILEGED_TOPUP;
}

mode_type card_detected(event_t event) {
  if (current_state.cart.total == 0 && current_state.cart.deposit == 0) {
    return current_state.mode;
  }
  return WRITE_CARD;
}

mode_type charge_list_two_digit(event_t event) {
  switch (event) {
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
      return two_digit_number_pressed(event - KEY_0);
    case KEY_HASH:
    case KEY_C:
    case KEY_D:
      return CHARGE_LIST;
    default:
      break;
  }
  return CHARGE_LIST_TWO_DIGIT;
}
mode_type charge_without_card(event_t event) {
  return CHARGE_WITHOUT_CARD;
}

mode_type charge_list(event_t event) {
  switch (event) {
    case KEY_HASH:
      return CHARGE_LIST_TWO_DIGIT;
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
      select_product(event - KEY_1 + 1);
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

mode_type main_starting_up(event_t event) {
  switch (event) {
    case STARTUP_COMPLETED:
      // log
      ESP_LOGI(TAG, "startup completed");
      return default_mode();

    // stay in same state
    default:
      break;
  }
  return MAIN_STARTING_UP;
}

mode_type charge_manual(event_t event) {
  switch (event) {
    // change state
    case TOKEN_DETECTED:
      return token_detected(event);
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
    default:
      break;
  }
  return CHARGE_MANUAL;
}

mode_type privileged_topup(event_t event) {
  switch (event) {
    case TOKEN_DETECTED:
      reset_cart();
      current_state.is_privileged = false;
      return CHARGE_LIST;
    case CARD_DETECTED:
      return WRITE_CARD;

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

mode_type main_menu(event_t event) {
  switch (event) {
    case KEY_D:
      return default_mode();

    // stay in same state
    default:
      break;
  }
  return MAIN_MENU;
}

mode_type process_event(event_t event) {
  switch (current_state.mode) {
    case CHARGE_LIST:
      return charge_list(event);
    case CHARGE_MANUAL:
      return charge_manual(event);
    case CHARGE_LIST_TWO_DIGIT:
      return charge_list_two_digit(event);
    case CHARGE_WITHOUT_CARD:
      return charge_without_card(event);

    case PRIVILEGED_TOPUP:
      return privileged_topup(event);
    case PRIVILEGED_CASHOUT:
    case PRIVILEGED_REPAIR:
      break;

    case WRITE_CARD:
    case WRITE_FAILED:
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
  // booting
  state_events = xQueueCreate(5, sizeof(int));
  ESP_LOGI(TAG, "waiting for startup to complete");

  xEventGroupWaitBits(event_group, LOCAL_CONFIG_LOADED | TIME_SET, pdFALSE, pdTRUE, portMAX_DELAY);
  xQueueSend(state_events, (void*)&(event_t){STARTUP_COMPLETED}, portMAX_DELAY);

  event_t event;
  while (true) {
    xQueueReceive(state_events, &event, portMAX_DELAY);
    mode_type previous_mode = current_state.mode;
    current_state.mode = process_event(event);
    if (previous_mode != current_state.mode) {
      ESP_LOGI(
          TAG, "Event %d changed state from %d to %d", event, previous_mode, current_state.mode
      );
    }
  }
}