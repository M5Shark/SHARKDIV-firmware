# Flashing guide — ESP32-DIV Hacker Edition

This builds the firmware from source and uploads it to an **ESP32-DIV V2** board over
USB. Two paths are covered: the **Arduino IDE** (easiest) and **arduino-cli** (terminal).

---

## 1. Prerequisites

1. **Arduino IDE 2.x** — <https://www.arduino.cc/en/software>
2. **ESP32 board support, Espressif core `2.0.10`.**
   - Arduino IDE → Settings → *Additional boards manager URLs*, add:
     `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → search **esp32** → install **version 2.0.10**.
     (This firmware was built and tested against 2.0.10. Newer 3.x cores change the
     Wi-Fi/`esp_wifi`, LEDC and Bluedroid APIs and will not compile without edits.)
3. **Required libraries** (Tools → Manage Libraries; exact versions matter for two of them):

| Library | Version | Note |
|---|---|---|
| TFT_eSPI | 2.5.33 | then apply step 2 below |
| **NimBLE-Arduino** | **1.4.3** | 2.x renames many classes — do **not** use 2.x |
| **arduinoFFT** | **1.5.5** | 2.x renames the class to `ArduinoFFT` — do **not** use 2.x |
| RF24 | any recent (tested 1.6.x) | |
| PCF8574 | any (tested 0.4.5) | |
| XPT2046_Touchscreen | any (tested 1.4.0) | |
| rc-switch | GitHub master (`sui77/rc-switch`) | not in the Library Manager |
| SmartRC-CC1101-Driver-Lib | any (tested 3.0.2) | provides `ELECHOUSE_CC1101_SRC_DRV` |
| ArduinoJson | 6.x | |
| Adafruit NeoPixel, Adafruit PN532 (+ Adafruit BusIO), IRremoteESP8266 | any recent | |

## 2. Configure TFT_eSPI (required — do not skip)

TFT_eSPI only drives the right display if its `User_Setup.h` matches the board. This
repo ships the correct one at `hardware/TFT_eSPI/User_Setup.h` (ILI9341 driver, HSPI,
ESP32-DIV v2 pins: MISO 16, MOSI 17, SCLK 19, CS 18, DC 8, RST -1, TOUCH_CS 7).

Copy it over the library's own copy, replacing the file:

- **Windows:** `Documents\Arduino\libraries\TFT_eSPI\User_Setup.h`
- **macOS:** `~/Documents/Arduino/libraries/TFT_eSPI/User_Setup.h`
- **Linux:** `~/Arduino/libraries/TFT_eSPI/User_Setup.h`

(If you prefer not to overwrite, you can instead point `User_Setup_Select.h` at it — but
overwriting is simplest.)

## 3. Linker patch (required on cores ≥ 2.0.6)

The WiFi Deauther tool overrides the SDK function
`ieee80211_raw_frame_sanity_check`. Since arduino-esp32 ~2.0.6 the SDK library defines
that symbol itself, and the build fails at link time with **"multiple definition of
ieee80211_raw_frame_sanity_check"** unless one extra linker flag is set.

Copy [`hardware/platform-patch/platform.local.txt`](hardware/platform-patch/platform.local.txt)
next to the core's `platform.txt`:

- **Windows:** `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\2.0.10\`
- **macOS:** `~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.10/`
- **Linux:** `~/.arduino15/packages/esp32/hardware/esp32/2.0.10/`

The file just adds `-Wl,--allow-multiple-definition` to the linker flags (the
arduino-cli path in step 5 passes the same flag automatically).

## 4. Board settings (Arduino IDE)

Open `ESP32-DIV/ESP32-DIV.ino`, then under **Tools** set:

| Setting                | Value                          |
|------------------------|--------------------------------|
| Board                  | **ESP32S3 Dev Module**         |
| USB CDC On Boot        | Disabled (or Enabled — either) |
| Flash Size             | **16MB (128Mb)**               |
| PSRAM                  | **OPI PSRAM**                  |
| Partition Scheme       | **Minimal SPIFFS (Large APPS)** |
| Upload Speed           | **115200**                     |
| CPU Frequency          | 240 MHz (default)              |

> **Upload Speed must be 115200.** Faster speeds (921600) fail on this board's USB
> bridge.

Select the right **Port** (Tools → Port), then click **Upload**. If upload doesn't start,
hold **BOOT**, tap **RESET**, release **BOOT** to enter download mode, then upload again.

## 5. Build & upload with arduino-cli (alternative)

On macOS there is a ready-made script: `./build-div-mac.sh` (uses the pinned core
in `~/.arduino210`, the exact library versions above, and the linker flag).

Manual equivalent:

```bash
# one-time: install the core
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@2.0.10 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
# one-time: libraries (plus rc-switch master from GitHub, see table above)
arduino-cli lib install TFT_eSPI@2.5.33 NimBLE-Arduino@1.4.3 arduinoFFT@1.5.5 \
  RF24 PCF8574 XPT2046_Touchscreen SmartRC-CC1101-Driver-Lib "Adafruit NeoPixel" \
  "Adafruit PN532" IRremoteESP8266 ArduinoJson
# (then copy hardware/TFT_eSPI/User_Setup.h into the installed TFT_eSPI library folder)

# compile + upload (replace PORT, e.g. /dev/ttyACM0, /dev/cu.usbserial-xxxx, COM5)
FQBN="esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=min_spiffs,UploadSpeed=115200"
arduino-cli compile --fqbn "$FQBN" \
  --build-property "compiler.c.elf.extra_flags=-Wl,--allow-multiple-definition" ESP32-DIV
arduino-cli upload  --fqbn "$FQBN" -p PORT ESP32-DIV
```

## 6. First boot

You should see the green boot splash, then the main menu in the **Hacker** theme
(green-on-black). If the UI comes up in a different theme, switch it under
**Settings → Theme → Hacker**. Flock Hunter is in the **WiFi** menu.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Blank / white screen | TFT_eSPI `User_Setup.h` not applied — redo step 2. |
| Garbled or wrong colours | Wrong driver/pins in `User_Setup.h` — use the one in this repo. |
| Upload fails / times out | Set Upload Speed to **115200**; enter download mode (BOOT+RESET). |
| Compile errors about `esp_wifi`/WiFi/NimBLE class names | You're on ESP32 core 3.x or NimBLE 2.x — use core **2.0.10** + **NimBLE 1.4.3**. |
| Link error: *multiple definition of ieee80211_raw_frame_sanity_check* | Apply the `platform.local.txt` from step 3. |
| `arduinoFFT` / `ArduinoFFT` compile errors | Use **arduinoFFT 1.5.5**, not 2.x. |
| Menu text hard to read | Set **Settings → Theme → Hacker**; the dim colour is tuned for readability. |

---

If you have your board's compiled `firmware.bin` (e.g. from a previous build), you can
also flash it directly with `esptool.py write_flash 0x0 firmware.bin` — but building
from source as above is the supported path.
