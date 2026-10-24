# Integration status — v1.7.30-dev

Reviewed 2026-10-24 against the supplied upstream projects. This is a source
development version; it has not been released or flashed to a device. A link in
this table means a reviewed reference, not that its entire repository was
imported. Existing similarly named features are distinguished from this change.

| Requested project | Status in this change | Hardware / implementation and provenance |
| --- | --- | --- |
| [Poor Man's 2.4 GHz Scanner](https://forum.arduino.cc/t/poor-mans-2-4-ghz-scanner/54846) | Existing passive scanner refined | nRF24L01+ received-power detection; occupancy is a count of threshold crossings, not calibrated dBm or device identification. Original 2011 AVR register code is a reference, not a new source import. |
| [arduino_oled_menu](https://github.com/upiir/arduino_oled_menu) | Independent menu inspiration | Upstream targets Arduino UNO, SSD1306 128×64 OLED and u8g/u8g2. SHARKDIV retains its TFT/touch drivers with a flatter, readable list interface. No upstream source, bitmap or font copied by this change. Upstream LICENSE: MIT. |
| [Universal-RC-system](https://github.com/alexbeliaev/Universal-RC-system) | Not ported | Four standalone sketches for RF24 transmit/receive, AVR PPM and an OLED spectrum analyzer. The analyzer concept is covered by the existing RF scanner. A usable RC transmitter/receiver needs agreed controls, peer protocol, pin allocation, ESP32 timer port and fail-safe behavior. No root license was present in the inspected repository; no source copied. |
| [AppleJuice](https://github.com/ECTO-1A/AppleJuice) | No new port or expansion | Proximity-pairing message spoofing. Existing BLE spoofing features predate this change; this patch adds no AppleJuice payloads or popup flooding. Upstream LICENSE: Apache-2.0. |
| [ESP32-Sour-Apple](https://github.com/RapierXbox/ESP32-Sour-Apple) | No new port or expansion | BLE crash/spam project. A Sour Apple entry already existed in the base firmware; it is not newly integrated or enhanced here. Upstream LICENSE: GPLv3. |
| [PwnGridSpam](https://github.com/7h30th3r0n3/PwnGridSpam) | Not added | Wi-Fi peer/screen spam and denial-of-service modes are not imported. Existing passive Pwnagotchi name matching is a separate feature, not a PwnGrid protocol implementation. Upstream README contains an MIT license. |
| [ESP32-AirTag-Scanner](https://github.com/MatthewKuKanich/ESP32-AirTag-Scanner) | Existing scanner refined independently | SHARKDIV already had an AirTag/Find My sniffer. Advertisement length validation, passive reception and candidate terminology are improved. Find My advertisements do not prove that a device is an AirTag, who owns it, or that it is following someone. Upstream has no root license in the inspected tree; no source copied. |
| [BLE Spam Flipper Application](https://github.com/Next-Flip/Momentum-Apps/tree/c470da2d792fc8c4f165ae2906d79250c33a823c/ble_spam) | Not ported | The pinned application uses Flipper's app/runtime APIs, not Arduino ESP32 APIs. No popup-flooding implementation or assets imported. Existing BLE Spam tooling is not evidence that this pinned Flipper application was integrated. |
| [opendroneid-core-c](https://github.com/opendroneid/opendroneid-core-c) | New standards-compatible receive decoder, verified against upstream | SHARKDIV uses an original bounded parser and dedicated Remote ID screen in place of the old manufacturer-ID/OUI heuristics. Two upstream core files are retained only as a host-test reference, with full Apache-2.0 license and source blob IDs; they are not linked into firmware. Details below. |
| [ESP Web Tools](https://esphome.github.io/esp-web-tools/) | New alternate installer and console | Pinned v10.1.1 web component, with `docs/manifest.json` targeting the existing stable v1.7.29 merged ESP32-S3 image. The primary esptool-js flasher remains, with simpler presentation, its board power fix retained, and non-S3 chips rejected. The web installer does not distribute this unreleased source build. Upstream LICENSE: Apache-2.0. |
| [Flock You](https://github.com/colonelpanichacks/flock-you) | Existing passive port retained | `flock_hunter.*` already ports the probe/OUI fingerprint, alongside the separate `flock_*` suite. This is not a wholesale import of the current upstream Wi-Fi/BLE/Flask implementation. Upstream LICENSE: MIT. Research credits remain in `CREDITS.md`. OUI/name matches are indicators; they do not establish device identity. |
| [RF24](https://github.com/nRF24/RF24) | Existing external driver dependency | nRF24L01+ over the board's shared SPI bus. The build environment's RF24 1.6.2 manifest declares GPL-2.0-only. This library is not newly vendored by the change. |
| [Original nRFBOX Project](https://github.com/cifertech/nrfbox) | Reference; no wholesale import | ESP32/nRF24/OLED radio toolkit. Passive scanning concepts overlap existing SHARKDIV features. Its jamming and disruption modes are not imported or expanded. Upstream LICENSE: MIT. |

## Remote ID receiver

Open **Detectors → Drone RemoteID**. Previous/Next selects a received broadcaster,
View toggles the list/detail display, Clear resets the current table, and Exit
returns to the detector launcher.

The receiver accepts:

- BLE legacy advertisements carrying Service Data with 16-bit UUID `0xFFFA`,
  Open Drone ID application code `0x0D`, a message counter and the ODID message.
- 2.4 GHz Wi-Fi beacons carrying vendor IE `FA:0B:BC`, type `0x0D`, a counter
  and the ODID message pack. It listens across channels 1–11.
- Protocol versions 0–2, 25-byte single messages, and complete packs of 1–9
  messages. The screen surfaces Basic ID and aircraft Location; other defined
  message types are skipped.

Details show the received aircraft ID, transport, RSSI, protocol version, last
reception age, latitude/longitude, WGS84 geodetic altitude, horizontal speed and
operational status. Unavailable coordinates/altitude/speed are not displayed as
valid measurements. Binary IDs are rendered as hexadecimal. The list retains at
most 12 transport/MAC pairs; entries and IDs expire after 30 seconds, and Location
data expires after 10 seconds. One aircraft using different MACs or transports can
appear more than once. There is no persistent identity tracking or recording in
this receiver.

The Wi-Fi radio uses NULL/monitor mode and BLE scanning is passive. This feature
does not transmit Remote ID, associate with access points, request scan responses,
or connect to discovered devices. It does not verify identity or authenticate
broadcasts; the screen says **Broadcast data / unverified**.

Wi-Fi NAN, 5 GHz, Bluetooth 5 extended/coded-PHY reception, proprietary drone
protocols and China's separate GB 46750 wire format are not supported by this
receiver. Reception is best effort while the ESP32 shares airtime between BLE and
Wi-Fi; hopping can miss frames. No received result does not establish the absence
of a drone. RSSI is a signal measurement, not a distance estimate.

## Source and license records

The added OpenDroneID reference files are in `tests/reference/opendroneid/`.
Their original copyright and SPDX headers are unchanged, and a full Apache-2.0
LICENSE and README with upstream blob identifiers accompany them. Only the two
portable core files are included for host-test fixture generation and comparison.
No upstream transmitter, Wi-Fi transport implementation, MAVLink or Linux
application is imported.

The production `sharkdiv/RemoteIdParser.cpp` is an original byte-oriented decoder
of the public wire format, under the project's MIT terms. It does not include or
link the Apache reference implementation. Keeping that implementation outside the
Arduino sketch avoids adding an Apache-2.0/GPL-2.0-only combination to the
firmware binary. RF24 remains the pre-existing external GPL-2.0-only dependency;
other dependencies retain their own license terms. The top-level MIT license does
not relicense any third-party library.

## Validation

`sh tests/run_remote_id_tests.sh` runs hand-built Basic ID/Location fixtures,
BLE and beacon transport cases, every truncation prefix of representative
messages, malformed packs and fields, unsupported versions/types, output
atomicity checks, unavailable values, and a deterministic hostile-input smoke
test. It also checks 175 upstream-encoded Location fixtures spanning coordinate
extremes and low/high speed ranges, plus Basic ID at all three supported protocol
versions. The reference C library and independent parser are compiled with
AddressSanitizer and UBSan;
both halt on the first finding. These host tests do not verify actual RF
reception, display legibility on the physical panel, antenna performance or board
power behavior.
