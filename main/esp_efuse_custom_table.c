/*
 * SPDX-FileCopyrightText: 2017-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sdkconfig.h"
#include "esp_efuse.h"
#include <assert.h>
#include "esp_efuse_custom_table.h"

// md5_digest_table 9a57694f6dab855cd0d0dec5bdf3bd4f
// This file was generated from the file esp_efuse_custom_table.csv. DO NOT CHANGE THIS FILE MANUALLY.
// If you want to change some fields, you need to change esp_efuse_custom_table.csv file
// then run `efuse_common_table` or `efuse_custom_table` command it will generate this file.
// To show efuse_table run the command 'show_efuse_table'.

static const esp_efuse_desc_t DEVICE_ID[] = {
    {EFUSE_BLK3, 0, 128}, 	 // ,
};





const esp_efuse_desc_t* ESP_EFUSE_DEVICE_ID[] = {
    &DEVICE_ID[0],    		// 
    NULL
};
