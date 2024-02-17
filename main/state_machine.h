#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
  CARD_DETECTED,
  CARD_REMOVED,
  PRIVILEGE_TOKEN_DETECTED,
  TIMEOUT,
  STARTUP_COMPLETED,
  WRITE_SUCCESSFUL,
  WRITE_UNSUCCESSFUL,
} event_t;

typedef enum {
  MAIN_STARTING_UP,
  MAIN_MENU,
  MAIN_FATAL,

  CHARGE_LIST,
  PRODUCT_LIST,
  CHARGE_MANUAL,
  CHARGE_WITHOUT_CARD,

  PRIVILEGED_TOPUP,
  PRIVILEGED_CASHOUT,
  PRIVILEGED_REPAIR,

  WRITE_CARD,
  WRITE_FAILED,

  CARD_BALANCE,
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
  menu_item_t* items;
  size_t count;
  int8_t active_item;
} menu_items_t;

typedef struct {
  int8_t first_digit;
  int8_t second_digit;
  uint8_t current_index;
} product_selection_t;

typedef struct {
  uint8_t id[7];
  uint16_t counter;
  uint8_t deposit;
  uint16_t balance;
  uint8_t signature[5];
} ultralight_card_info_t;

typedef enum {
  NONE,
  CARD_LIMIT_EXCEEDED,
  INSUFFICIENT_FUNDS,
  INSUFFICIENT_DEPOSIT,
  TECHNICAL_ERROR,
} write_failed_reason_t;

typedef struct {
  mode_type mode;
  bool is_privileged;
  cart_t cart;
  product_selection_t product_selection;
  menu_items_t main_menu;
  LogMessage_CardTransaction_TransactionType transaction_type;
  int log_files_to_upload;
  int manual_amount;
  ultralight_card_info_t data_to_write;
  write_failed_reason_t write_failed_reason;
} state_t;

extern QueueHandle_t state_events;
extern state_t current_state;
void state_machine(void* params);
int current_total();
