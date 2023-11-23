#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

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
  mode_type mode;
  bool is_privileged;
  // Transaction transaction;
} state_t;

extern QueueHandle_t state_events;
extern state_t current_state;
void state_machine(void* params);
