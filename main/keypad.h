#pragma once
#include "driver/gpio.h"

static const gpio_num_t KEYPAD_ROWS[4] = {18, 17, 16, 15};
static const gpio_num_t KEYPAD_COLS[4] = {7, 6, 5, 4};

void keypad(void* params);