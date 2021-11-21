#!/bin/bash

set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PREFIX="#define BUILD_NUMBER "
FILE="$SCRIPT_DIR/../src/BuildNumber.h"
CURRENT=$(sed -e "s/^$PREFIX//" "$FILE")
NEXT=$(($CURRENT + 1))
echo "$PREFIX$NEXT" > "$FILE"