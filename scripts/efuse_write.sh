#!/bin/bash

echo "Enter a string:"
read DEVICE_NAME

regex='^[A-Za-z0-9äöü-]{1,16}$'

if ! [[ $DEVICE_NAME =~ $regex ]]; then
    echo "Error: String does not match the required pattern." >&2
    exit 1
fi

echo "Writing device name to efuse..."
echo "Device name: $DEVICE_NAME"

TEMP_DIR=$(mktemp -d)
echo -n "$DEVICE_NAME" | dd bs=32 conv=sync status=none of="$TEMP_DIR/device_id.bin"
echo "Device name written to $TEMP_DIR/device_id.bin"
python $IDF_PATH/components/esptool_py/esptool/espefuse.py --port /dev/tty.usbmodem41201 --chip esp32s3 burn_block_data BLOCK3 "$TEMP_DIR/device_id.bin"