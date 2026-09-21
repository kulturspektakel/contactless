#!/bin/sh
set -eu

test_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test_build_directory=$(mktemp -d "${TMPDIR:-/tmp}/contactless-pn532.XXXXXX")
test_binary="$test_build_directory/test_pn532"
trap 'rm -f "$test_binary"; rmdir "$test_build_directory"' EXIT HUP INT TERM

"${CC:-clang}" -std=gnu11 -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
  -I"$test_directory/stubs" "$test_directory/test_pn532.c" -o "$test_binary"
"$test_binary"
