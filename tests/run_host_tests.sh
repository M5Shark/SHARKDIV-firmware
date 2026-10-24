#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=$(mktemp -d "${TMPDIR:-/tmp}/sharkdiv-host-tests.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
CXX=${CXX:-c++}
"$CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer "$ROOT/tests/passive_radio_test.cpp" -o "$BUILD/passive-radio"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 "$BUILD/passive-radio"
for test in flock_radio flock_gps detection_alert; do
  "$CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer "$ROOT/tests/${test}_test.cpp" -o "$BUILD/$test"
  UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 "$BUILD/$test"
done
python3 "$ROOT/tests/flock_ui_test.py"
sh "$ROOT/tests/run_detector_tests.sh"
sh "$ROOT/tests/run_remote_id_tests.sh"
python3 "$ROOT/tests/touch_nav_edge_test.py"
python3 "$ROOT/tests/menu_layout_test.py"
python3 "$ROOT/tests/settings_ui_test.py"
python3 "$ROOT/tests/detection_audio_test.py"
python3 "$ROOT/tests/receiver_alert_paths_test.py"
python3 "$ROOT/tests/receiver_alert_regressions.py"
python3 "$ROOT/tests/ble_host_operation_test.py"
python3 "$ROOT/tests/background_radio_handoff_test.py"
node "$ROOT/tests/flasher_test.mjs"
