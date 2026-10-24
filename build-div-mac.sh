#!/bin/bash
# build-div-mac.sh — build ESP32-DIV Hacker Edition for the ESP32-DIV V2
# (ESP32-S3, 16MB flash, OPI PSRAM) with arduino-cli on macOS.
#
# Requirements (one-time):
#   1. esp32 core 2.0.10 installed into ~/.arduino210
#      (arduino-cli core install esp32:esp32@2.0.10 --additional-urls \
#        https://espressif.github.io/arduino-esp32/package_esp32_index.json \
#        --config-file <cfg pointing data: ~/.arduino210>)
#   2. Libraries (in ~/Documents/Arduino/libraries or $ESP32DIV_LIBS):
#      NimBLE-Arduino 1.4.3, arduinoFFT 1.5.5, RF24, PCF8574,
#      XPT2046_Touchscreen, rc-switch (master), SmartRC-CC1101-Driver-Lib,
#      ArduinoJson, TFT_eSPI (with hardware/TFT_eSPI/User_Setup.h),
#      Adafruit NeoPixel, Adafruit PN532 (+BusIO), IRremoteESP8266
#   3. TFT_eSPI's User_Setup.h replaced with hardware/TFT_eSPI/User_Setup.h
#
# The --build-property flag replaces hardware/platform-patch/platform.local.txt
# (needed because the sketch overrides ieee80211_raw_frame_sanity_check, which
# the SDK defines as a strong symbol on cores >= ~2.0.6).

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$HERE/sharkdiv"

ARDUINO_CLI="${ARDUINO_CLI:-$HOME/Projects/M5Shark/.tools/arduino-cli/arduino-cli}"
CONFIG="${CONFIG:-$HOME/Projects/M5Shark/.tools/arduino-cli-210.yaml}"
EXTRA_LIBS="${ESP32DIV_LIBS:-$HERE/libs}"

"$ARDUINO_CLI" compile \
  --config-file "$CONFIG" \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=min_spiffs \
  --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" \
  --libraries "$EXTRA_LIBS" \
  --libraries "$HOME/Documents/Arduino/libraries" \
  --output-dir "$HERE/build" \
  "$SKETCH"

echo
echo "Done: $HERE/build/sharkdiv.ino.bin (application only, offset 0x10000)"
echo "Use arduino-cli upload for the complete build; only a merged image belongs at 0x0."
