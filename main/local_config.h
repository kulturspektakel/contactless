#pragma once

#include "config.pb.h"
#include "configs.pb.h"
#include "constants.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define MAX_LIST_NAME_LENGTH sizeof(((DeviceConfig*)0)->name)
#define MAX_PRIVILEGE_TOKENS \
  sizeof((AllLists*)0)->privilege_tokens / sizeof(AllLists_privilege_tokens_t)
#define MAX_SUSPENDED_CREW_CARDS \
  sizeof(((AllLists*)0)->suspended_crew_cards) / sizeof(AllLists_suspended_crew_cards_t)

typedef struct {
  int32_t id;
  char name[MAX_LIST_NAME_LENGTH];
} product_list_t;

extern product_list_t* product_lists;
extern int32_t lists_count;
extern DeviceConfig active_config;
extern AllLists_suspended_crew_cards_t suspended_crew_cards[MAX_SUSPENDED_CREW_CARDS];
extern AllLists_privilege_tokens_t privilege_tokens[MAX_PRIVILEGE_TOKENS];
extern int32_t all_lists_checksum;
extern QueueHandle_t config_update_queue;
extern int32_t config_timestamp;

void local_config(void* params);
