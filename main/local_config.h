#pragma once

#include "config.pb.h"

extern DeviceConfig active_config;
extern int32_t all_lists_checksum;
void local_config(void* params);