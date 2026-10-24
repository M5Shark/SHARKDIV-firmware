#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=$(mktemp -d "${TMPDIR:-/tmp}/sharkdiv-host-tests.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
CXX=${CXX:-c++}
"$CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer "$ROOT/tests/passive_radio_test.cpp" -o "$BUILD/passive-radio"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 "$BUILD/passive-radio"
sh "$ROOT/tests/run_remote_id_tests.sh"
python3 "$ROOT/tests/touch_nav_edge_test.py"
python3 "$ROOT/tests/ble_host_operation_test.py"
node "$ROOT/tests/flasher_test.mjs"
