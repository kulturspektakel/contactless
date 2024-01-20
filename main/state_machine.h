#pragma once

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
  KEY_TRIPPLE_HASH,
  CARD_DETECTED,
  TOKEN_DETECTED,
  TIMEOUT,
  STARTUP_COMPLETED,
} event_t;

typedef enum {
  MAIN_STARTING_UP,
  MAIN_MENU,
  MAIN_FATAL,

  CHARGE_LIST,
  CHARGE_LIST_TWO_DIGIT,
  CHARGE_MANUAL,
  CHARGE_WITHOUT_CARD,

  PRIVILEGED_TOPUP,
  PRIVILEGED_CASHOUT,
  PRIVILEGED_REPAIR,

  WRITE_CARD,
  WRITE_FAILED,
} mode_type;

typedef struct {
  int deposit;
  int total;
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
  int active_item;
} menu_items_t;

typedef struct {
  mode_type mode;
  bool is_privileged;
  cart_t cart;
  int product_list_first_digit;
  menu_items_t main_menu;
  mode_type previous_mode;
  int log_files_to_upload;
} state_t;

extern QueueHandle_t state_events;
extern state_t current_state;
void state_machine(void* params);
