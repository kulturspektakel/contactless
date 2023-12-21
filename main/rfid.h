#pragma once

#include <stdint.h>

typedef struct {
  uint8_t id[7];
  uint16_t counter;
  uint8_t deposit;
  uint16_t balance;
  uint8_t signature[5];
} ultralight_card_info_t;

void rfid(void* params);
extern ultralight_card_info_t current_card;
