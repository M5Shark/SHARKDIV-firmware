#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=$(mktemp -d "${TMPDIR:-/tmp}/sharkdiv-remote-id.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
CC=${CC:-cc}
CXX=${CXX:-c++}
"$CC" -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer -g \
  -c "$ROOT/tests/reference/opendroneid/opendroneid.c" -o "$BUILD/opendroneid.o"
"$CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -g "$ROOT/tests/remote_id_parser_test.cpp" \
  "$ROOT/sharkdiv/RemoteIdParser.cpp" "$BUILD/opendroneid.o" -lm -o "$BUILD/test"
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=halt_on_error=1 "$BUILD/test"
