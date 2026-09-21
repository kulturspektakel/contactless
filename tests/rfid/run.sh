#!/bin/sh
set -eu

test_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repository_directory=$(CDPATH= cd -- "$test_directory/../.." && pwd)
test_build_directory=$(mktemp -d "${TMPDIR:-/tmp}/contactless-rfid.XXXXXX")
test_binary="$test_build_directory/test_rfid"
trap 'rm -f "$test_binary"; rmdir "$test_build_directory"' EXIT HUP INT TERM

"${CC:-clang}" -std=gnu11 -O1 -fno-omit-frame-pointer \
  -fsanitize=address,undefined \
  -fno-sanitize-recover=all -Wno-missing-declarations \
  -I"$test_directory/stubs" \
  -I"$repository_directory/components/nanopb" \
  -I"$repository_directory/components/nanopb/nanopb" \
  "$test_directory/test_rfid.c" -o "$test_binary"
"$test_binary"
