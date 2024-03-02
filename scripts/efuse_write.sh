#!/bin/bash

echo "Enter a string:"
read DEVICE_NAME

REGEX='^[A-Za-z0-9äßöü-]$'

if ! [[ $DEVICE_NAME =~ $REGEX ]]; then
    echo "Error: String contains invalid characters." >&2
    exit 1
fi

BYTE_COUNT=$(echo -n "$DEVICE_NAME" | wc -c)
if [ $BYTE_COUNT -gt 16 ]; then
    echo "Error: String is longer than 16 bytes."
    exit 1
fi

echo "Writing device name to efuse..."
echo "Device name: $DEVICE_NAME"

TEMP_DIR=$(mktemp -d)
echo -n "$DEVICE_NAME" | dd bs=32 conv=sync status=none of="$TEMP_DIR/device_id.bin"
echo "Device name written to $TEMP_DIR/device_id.bin"
python $IDF_PATH/components/esptool_py/esptool/espefuse.py --port /dev/tty.usbmodem41201 --chip esp32s3 burn_block_data BLOCK3 "$TEMP_DIR/device_id.bin"