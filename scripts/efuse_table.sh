#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $IDF_PATH/components/efuse/    
./efuse_table_gen.py $DIR/../main/esp_efuse_custom_table.csv -t esp32s3
mv $DIR/../main/include/esp_efuse_custom_table.h $DIR/../main/esp_efuse_custom_table.h
rmdir $DIR/../main/include