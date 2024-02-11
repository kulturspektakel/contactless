#pragma once

#include "config.pb.h"
#include "configs.pb.h"
#include "state_machine.h"

#define MAX_PRIVILEGE_TOKENS 30

extern DeviceConfig active_config;
extern AllLists_privilege_tokens_t privilege_tokens[MAX_PRIVILEGE_TOKENS];
extern int32_t all_lists_checksum;
menu_items_t initialize_main_menu();
void select_list(int list_id);
void local_config(void* params);
