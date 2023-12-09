#pragma once

#include "config.pb.h"

typedef struct _menu_item_t {
  char name[21];
  int32_t list_id;
} menu_item_t;

extern DeviceConfig active_config;
extern int32_t all_lists_checksum;
void initialize_main_menu();
void local_config(void* params);