#pragma once

#define LENGTH_ID 7
#define LENGTH_COUNTER 2
#define LENGTH_DEPOSIT 1
#define LENGTH_BALANCE 2
#define LENGTH_SIGNATURE 5

#include "state_machine.h"

void rfid(void* params);
extern ultralight_card_info_t current_card;
