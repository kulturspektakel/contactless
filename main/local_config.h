#pragma once

#include "config.pb.h"
#include "state_machine.h"

extern DeviceConfig active_config;
extern int32_t all_lists_checksum;
menu_items_t initialize_main_menu();
void select_list(int list_id);
void local_config(void* params);
