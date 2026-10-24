# SHARKDIV Firmware

Open-source multi-tool firmware for the **SHARKDIV** wireless research device
(ESP32-S3, 16 MB flash, 2.4" ILI9341 touch display, nRF24L01+ and CC1101
radios, PN532 RFID, IR transceiver, GPS).

> **Research and education tool.** Use it only on networks and devices you
> own or are explicitly authorised to test, and only where local law allows.
> You are responsible for how you use it. The jamming tools some other
> firmwares carry are deliberately **not** included.

Based on the original [ESP32-DIV firmware by CiferTech](https://github.com/cifer-tech/ESP32-DIV) (MIT), with the SHARKDIV brand interface, additional tools, and a full QA toolchain. Binaries for end users live in this repo's [Releases](https://github.com/M5Shark/SHARKDIV-firmware/releases).

## Feature map

| Area | Tools |
|---|---|
| WiFi | Packet Monitor, Beacon Spammer, Deauther, Probe Flood, Deauth **Detector**, Scanner, Captive Portal, Hidden SSID Revealer, WPS Scanner, ARP Scanner, Karma Attack, Flock Hunter, WiFi Radar, SSID Rotator, Channel Analyzer |
| Bluetooth | Spoofer, Sour Apple, AirTag Spoofer, Find My Scanner (passive candidates), Sniffer, Scanner, BLE Rubber Ducky, Skimmer Detect, BLE Spam, BLE Link Test |
| 2.4 GHz (nRF24) | 125-channel Spectrum Scan (occupancy, hold/reset, calibration), Proto Kill, ESB Sniffer, ESB Replay, MouseJack Scan/Inject |
| SubGHz (CC1101) | Replay Attack, De Bruijn/Brute, **Jamming Detector**, Saved Profiles, **SDR Spectrum** (sweep analyzer with peak-hold, zoom and signal event log) |
| RFID | Read / Clone / Erase / Dump / Decode / privacy tools |
| GPS | Wardriver, Satellite Scanner, **GPS Guard** (jam/spoof anomaly monitor), Coordinates (DD/DMS/Maidenhead) |
| IR | Record, Saved Profile, Universal Controller, TV Power, IR Repeater |
| Detectors | Drone Remote ID (passive BLE legacy + 2.4 GHz ASTM beacons; Basic ID and aircraft location), existing fingerprint detectors |
| Tools | Serial Monitor, OTA Update, Touch Calibration, SD File Manager, Snake, Reaction Test, QR Code, **user apps** |

Interface: flat instrument menus, compact labels, high-contrast selection, a 150 ms name/version splash, 4 themes, touch navigation, battery fuel gauge, SD-config settings, and a **remote-test serial console** (`u/d/l/r/k` tap, `s` state, `b` battery, `a` sound status, `t` sound test).

Detection sound defaults to On. **Settings → Detection sound** mutes all receiver alerts; the choice persists on the device without an SD card. **Test sound** plays two short chirps. New observations produce short alerts, with duplicate and burst suppression. See [detection sounds and fixes](docs/DETECTION_ALERTS.md) for coverage and verification.

See [integration status](docs/INTEGRATIONS.md) for every referenced project, supported transports, limitations and source licenses. Find My advertisements are candidates, not proof of an AirTag; Remote ID displays unverified broadcast data.

## Flash from the browser

**[sharkdiv web flasher](https://m5shark.github.io/SHARKDIV-firmware/)** —
Use a desktop browser with Web Serial. The main installer retains the board's
backlight power fix. ESP Web Tools 10.1.1 provides an alternate installer and
serial console.

The flasher and `docs/manifest.json` still serve the existing **v1.7.29** stable
binary. This source branch is **v1.7.31-alerts**; build it locally until a tested
release is published.

## Building

```bash
./build-div-mac.sh        # arduino-cli, esp32 core 2.0.10 (pinned)
```

See `FLASHING.md` for flashing over USB (115200 baud) and OTA.

## Remote testing & app builder (MCP)

[`tools/sharkdiv_mcp.py`](tools/sharkdiv_mcp.py) is a zero-dependency MCP
server that drives the device over USB — tap navigation, state reports,
reset-with-boot-log, and an app builder that creates/builds/flashes new
user apps end to end. See [tools/README.md](tools/README.md).

## User apps

A new app is one source file + one line in `UserApps.cpp` — it then appears
in the Tools menu automatically. `UserApps.cpp` (Demo App) is the template.

## License

Project source: MIT — see [LICENSE](LICENSE). Credits and third-party scope are
in [CREDITS.md](CREDITS.md) and [integration status](docs/INTEGRATIONS.md).
Vendored libraries and test references retain their own licenses; the project
license does not relicense those dependencies.
