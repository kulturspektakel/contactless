#include "state_machine.h"
#include "esp_log.h"
#include "event_group.h"

static const char* TAG = "state_machine";
static bool is_privileged = false;

QueueHandle_t state_events;
state_t current_state = {
    .mode = MAIN_STARTING_UP,
    .is_privileged = false,
};

mode_type default_mode() {
  return current_state.is_privileged ? PRIVILEGED_TOPUP : CHARGE_LIST;
}

void reset_transaction() {}
void select_product(int product) {}
void update_deposit() {}
void remove_digit() {}
void add_digit(int d) {}

mode_type token_detected(event_t event) {
  reset_transaction();
  is_privileged = true;
  return PRIVILEGED_TOPUP;
}

mode_type card_detected(event_t event) {
  if (false /* transaction is not 0*/) {
    return WRITE_CARD;
  }
  return current_state.mode;
}

mode_type charge_list_two_digit(event_t event) {
  return current_state.mode;
}
mode_type charge_without_card(event_t event) {
  return current_state.mode;
}

mode_type charge_list(event_t event) {
  switch (event) {
    // change state
    case KEY_HASH:
      return charge_list_two_digit(event);
    case KEY_STAR:
      return charge_without_card(event);
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
      update_deposit();
      break;
    case KEY_B:
      update_deposit();
      break;
    case KEY_D:
      reset_transaction();
      break;
    default:
      break;
  }
  return CHARGE_LIST;
}

mode_type main_starting_up(event_t event) {
  switch (event) {
    case STARTUP_COMPLETED:
      return default_mode();
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
      update_deposit();
      break;
    case KEY_B:
      update_deposit();
      break;
    case KEY_D:
      reset_transaction();
      break;

    default:
      break;
  }
  return CHARGE_MANUAL;
}

mode_type privileged_topup(event_t event) {
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
      add_digit(event - KEY_0);
      break;
    case KEY_A:
      update_deposit();
      break;
    case KEY_B:
      update_deposit();
      break;
    case KEY_C:
      remove_digit();
      break;
    case KEY_D:
      reset_transaction();
      break;
    case TOKEN_DETECTED:
      reset_transaction();
      is_privileged = false;
      return CHARGE_LIST;
    case CARD_DETECTED:
      return charge_list(event);
    default:
      break;
  }
  return PRIVILEGED_TOPUP;
}

mode_type main_menu(event_t event) {
  switch (event) {
    case KEY_D:
      return default_mode();
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
  xQueueSend(state_events, STARTUP_COMPLETED, portMAX_DELAY);

  event_t event;
  while (true) {
    xQueueReceive(state_events, &event, portMAX_DELAY);
    mode_type previous_mode = current_state.mode;
    current_state.mode = process_event(event);
    if (previous_mode != current_state.mode) {
      ESP_LOGI(TAG, "State changed from %d to %d", previous_mode, current_state.mode);
    }
  }
}
