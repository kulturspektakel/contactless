#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
TEMP_DIR=$(mktemp -d)
NVS_BIN="$TEMP_DIR/nvs_data.bin"

$IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate --version 2 $DIR/../nvs_data.csv $NVS_BIN 0x3000

$IDF_PATH/components/partition_table/parttool.py --port "/dev/tty.usbmodem41201" write_partition --partition-name=nvs --input $NVS_BIN

 rm $NVS_BIN
