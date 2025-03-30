#pragma once

#include <stdint.h>
#include "logmessage.pb.h"

typedef enum {
  KEY_0,
  KEY_1,
  KEY_2,
  KEY_3,
  KEY_4,
  KEY_5,
  KEY_6,
  KEY_7,
  KEY_8,
  KEY_9,
  KEY_STAR,
  KEY_HASH,
  KEY_A,
  KEY_B,
  KEY_C,
  KEY_D,
  KEY_TRIPPLE_D,

  CARD_REMOVED,
  CARD_DETECTED_OK,
  CARD_DETECTED_SKIPPED_SECUIRTY,
  CARD_DETECTED_NOT_READABLE,
  CARD_DETECTED_OLD_CARD,

  PRIVILEGED_CREW_CARD_DETECTED,
  SUSPENDED_CREW_CARD_DETECTED,
  TIMEOUT,
  STARTUP_COMPLETED,
  WRITE_SUCCESSFUL,
  WRITE_UNSUCCESSFUL,
  FATAL_ERROR,
  ENTER_POWER_SAVE,
} event_t;

typedef enum {
  MAIN_STARTING_UP,
  MAIN_MENU,
  MAIN_PRODUCT_LISTS,
  MAIN_FATAL,

  CHARGE_LIST,
  PRODUCT_LIST,
  CHARGE_MANUAL,
  CHARGE_WITHOUT_CARD,

  PRIVILEGED_TOPUP,
  PRIVILEGED_CASHOUT,
  PRIVILEGED_REPAIR,
  PRIVILEGED_ENROLL_CREW_CARD,

  WRITE_CARD,
  WRITE_NOT_ATTEMPTED,
  WRITE_FAILED,

  CARD_BALANCE,

  READ_FAILED,
  BATTERY_TEST,
  POWER_SAVE,
} mode_type;

typedef struct {
  int deposit;
  LogMessage_Order_CartItem items[9];
  int item_count;
} cart_t;

typedef struct {
  char name[21];
  int32_t list_id;
} menu_item_t;

typedef struct {
  int8_t first_digit;
  int8_t second_digit;
  uint8_t current_index;
} product_selection_t;

typedef enum { REGULAR, CREW } ultralight_card_type_t;

typedef struct {
  uint8_t id[7];
  ultralight_card_type_t type;
  uint8_t signature[5];

  union {
    struct {
      uint16_t counter;
      uint8_t deposit;
      uint16_t balance;
    } regular;
    struct {
      uint16_t valid_until
    } crew;
  } data;
} ultralight_card_info_t;

typedef enum {
  NONE,
  CARD_LIMIT_EXCEEDED,
  INSUFFICIENT_FUNDS,
  INSUFFICIENT_DEPOSIT,
  TECHNICAL_ERROR,
  OLD_CARD,
  INVALID_SIGNATURE,
  CARD_EXPIRED,
  CARD_SUSPENDED
} card_error_t;

typedef struct {
  mode_type mode;
  bool is_privileged;
  cart_t cart;
  product_selection_t product_selection;
  LogMessage_CardTransaction_TransactionType transaction_type;
  int manual_amount;
  ultralight_card_info_t data_to_write;
  ultralight_card_info_t data_before_write;
  card_error_t card_error;
  int64_t expected_bootup_time;
  uint8_t menu_index;
  int8_t menu_index_active;
  int8_t submenu_index;
} state_t;

extern state_t current_state;
void trigger_event(event_t event);
void state_machine(void* params);
int current_total();
