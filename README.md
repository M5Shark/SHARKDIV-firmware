# SHARKDIV Firmware

Open-source multi-tool firmware for the **SHARKDIV** wireless research device
(ESP32-S3, 16 MB flash, 2.4" ILI9341 touch display, nRF24L01+ and CC1101
radios, PN532 RFID, IR transceiver, GPS).

> **Research and education tool.** Use it only on networks and devices you
> own or are explicitly authorised to test, and only where local law allows.
> You are responsible for how you use it. The jamming tools some other
> firmwares carry are deliberately **not** included.

Based on the original [ESP32-DIV firmware by CiferTech](https://github.com/cifer-tech/ESP32-DIV) (MIT), with the M5SHARK ctOS interface, additional tools, and a full QA toolchain. Binaries for end users live in [SHARKDIV-updates](https://github.com/M5Shark/SHARKDIV-updates).

## Feature map

| Area | Tools |
|---|---|
| WiFi | Packet Monitor, Beacon Spammer, Deauther, Probe Flood, Deauth **Detector**, Scanner, Captive Portal, Hidden SSID Revealer, WPS Scanner, ARP Scanner, Karma Attack, Flock Hunter, WiFi Radar, SSID Rotator, Channel Analyzer |
| Bluetooth | Spoofer, Sour Apple, AirTag Spoofer/Sniffer, Sniffer, Scanner, BLE Rubber Ducky, Skimmer Detect, BLE Spam, BLE Link Test |
| 2.4 GHz (nRF24) | 125-channel Scanner, Proto Kill, ESB Sniffer, ESB Replay, MouseJack Scan/Inject |
| SubGHz (CC1101) | Replay Attack, De Bruijn/Brute, **Jamming Detector**, Saved Profiles, **SDR Spectrum** (sweep analyzer with peak-hold, zoom and signal event log) |
| RFID | Read / Clone / Erase / Dump / Decode / privacy tools |
| GPS | Wardriver, Satellite Scanner, **GPS Guard** (jam/spoof anomaly monitor), Coordinates (DD/DMS/Maidenhead) |
| IR | Record, Saved Profile, Universal Controller, TV Power, IR Repeater |
| Tools | Serial Monitor, OTA Update, Touch Calibration, SD File Manager, Snake, Reaction Test, QR Code, **user apps** |

Extras: ctOS-style Skull Rain boot animation, phosphor-green boot palette, 4 themes, touch-first UI (tap latching — no physical buttons needed), battery fuel gauge, SD-config settings, and a **remote-test serial console** (`u/d/l/r/k` tap, `s` state, `b` battery) so the whole device can be driven by automated tooling.

## Building

```bash
./build-div-mac.sh        # arduino-cli, esp32 core 2.0.10 (pinned)
```

See `FLASHING.md` for flashing over USB (115200 baud) and OTA.

## User apps

A new app is one source file + one line in `UserApps.cpp` — it then appears
in the Tools menu automatically. `UserApps.cpp` (Demo App) is the template.

## License

MIT — see [LICENSE](LICENSE). Credits in [CREDITS.md](CREDITS.md).
Vendored libraries keep their own licenses (`libs/`).
