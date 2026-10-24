# Detection sound and receiver fixes — v1.7.31-alerts

Detection sound defaults to **On**. Open **Settings → Detection sound** to mute
all tools. That choice is saved in the device's NVS memory immediately, including
when no SD card is installed. Other settings use **Save** and the SD card. **Test
sound** plays two short chirps; muted devices remain silent. Settings supports
single taps, Up/Down selection, Left/Right adjustment and center Save/exit.

## Sound behavior

A single short chirp indicates a new received device, signal or valid fix.
Find My, Remote ID and camera/Flock/Gear candidates use two short chirps. A sound
reports received evidence, not proof of a particular device's identity or owner.
Flock's separate **Match sound** setting is also honored; global mute wins.

Repeated observations of an identity stay silent while it remains present.
An identity may alert again after 30 seconds without an observation. Simultaneous
results are grouped: at most one alert starts per second. The sound history
tracks 64 active identities. Additional observations are coalesced until a slot
expires, so busy environments cannot churn into continuous beeping; receiver
results continue to be collected independently. Sound stops on leaving
a tool or muting. Background home-screen scans do not beep.

| Receiver | Alert event |
|---|---|
| Wi-Fi scanner / Radar / wardriver | Newly received AP observation |
| Packet monitor / deauth detector | Valid received source or matching deauth frame |
| WPS scanner | Received AP record with WPS advertised |
| BLE scanner / sniffer / link checks / wardriver | Received device observation |
| Find My / Skimmer detector | Matching received candidate advertisement |
| Flock / Camera / Gear | Accepted configured signature match |
| Drone Remote ID | Successfully decoded broadcast record |
| nRF24 Spectrum / Source Finder | Transition into measured RF activity; no device identity implied |
| ESB receiver | Received packet |
| CC1101 receive / SDR / jamming detector | Decoded capture or measured signal event |
| IR capture | Received IR capture |
| RFID reader | Acquired tag UID |
| GPS | Checksum-valid, coordinate-valid fix acquisition |

Transmit/replay actions, menus, file tools, games and the manual Signal Logger
counter do not represent received detections and do not generate these alerts.
Ambient RF context inside Flock does not generate camera match sounds.

The service uses the ESP32-DIV V2 buzzer on GPIO45 and LEDC channel2/timer1.
The backlight remains on channel0/timer0 at 5kHz. Previous channel1 sound code
shared the backlight timer. All buzzer writes now have one owner in a dedicated
task, so receive callbacks never delay for sound. Invalid buzzer GPIOs are
reported unavailable instead of being driven.

## Corrected defects

- Bluetooth result caches no longer retain borrowed device pointers across
  scanner handoffs. Scanner callbacks, result limits and passive settings are
  restored when the next receiver starts.
- WPS reads Arduino's retained AP records instead of reading the consumed
  ESP-IDF scan list. Scanning no longer erases saved Wi-Fi credentials.
- Wi-Fi Radar scans asynchronously, preserves credentials, bounds retained
  results and handles scan deadlines without freezing navigation.
- Passive monitor startup rolls back partial failures; receiver error displays
  reflect actual state.
- GPS rejects checksum errors, malformed coordinates and invalid/simulated fixes
  before generating fix sounds.
- Settings toggles act once per press. Opening presses do not immediately exit;
  brightness stays visible; failed saves are shown.
- Long menu labels fit their real font width. Tools pages keep every installed
  app reachable. Holding a page button no longer flips repeatedly.
- Serial uppercase navigation pulses are shorter than the menu repeat interval.
  State reports do not index synthetic footer entries as actual menu items.

## Verification

`tests/run_host_tests.sh` covers production audio task behavior with a simulated
clock/PWM, bounded notification policy, receiver callbacks, radio lifecycle,
NMEA validation, UI navigation/settings and the browser installer. The firmware
is built for ESP32-DIV V2 with Arduino ESP32 2.0.10. The UI preview executes actual
drawing functions and TFT font bitmaps with sample status data; it is not a photo.

Physical verification results are recorded separately after flashing. A successful
build and fixture tests do not establish that every hardware accessory or every
possible defect has been tested. The public web installer remains stable v1.7.29
until a release is separately published.

### Connected-device checks (2026-10-25)

- ESP32-S3, 16 MB flash, this repository's ESP32-DIV V2 wiring profile.
- Application build: 1,849,901 bytes (94% of application partition); static RAM
  157,016 bytes (47%).
- Initial hardware run: booted, explicit sound test ran, live Wi-Fi/BLE receptions
  generated alert patterns; backlight remained at 5kHz throughout.
- BLE Scanner → Find My Scanner → BLE Scanner completed without a reset or
  dangling result crash. Returned to menu successfully.
- Settings mute stopped the sound test, survived a device restart, then was
  restored to On. The user confirmed that the buzzer was audible.
- Final review added full-history overflow suppression; the regression sends 100
  unchanged identities for 100 rounds and permits only the initial grouped cue.
- Flock/camera, Remote ID, IR, RFID, GPS and external RF-specific event fixtures
  pass on the host. Matching physical test targets were not present for every
  accessory, so those physical paths are not claimed as exhaustively validated.
