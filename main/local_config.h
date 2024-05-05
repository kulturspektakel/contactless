#pragma once

#include "config.pb.h"
#include "configs.pb.h"
#include "constants.h"
#include "state_machine.h"

#define MAX_LIST_NAME_LENGTH 20

typedef struct {
  int32_t id;
  char name[MAX_LIST_NAME_LENGTH];
} product_list_t;

extern product_list_t* product_lists;
extern int32_t lists_count;
extern DeviceConfig active_config;
extern AllLists_privilege_tokens_t privilege_tokens[MAX_PRIVILEGE_TOKENS];
extern int32_t all_lists_checksum;
void select_list(int list_id);
void local_config(void* params);
