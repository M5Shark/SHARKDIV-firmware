# Credits

This project is a derivative work. Full credit to the upstream authors.

## Original firmware — ESP32-DIV
- Author: **CiferTech**
- Repo: <https://github.com/cifertech/ESP32-DIV>
- License: **MIT**
- The entire base firmware (all WiFi / BLE / sub-GHz / IR / RFID / GPS tooling, the
  menu system, settings store, drivers, and UI framework) comes from this project.

## Flock Hunter — passive Flock Safety detector
- Ported from: **flock-you** by **colonelpanichacks**
- Repo: <https://github.com/colonelpanichacks/flock-you>
- License: **MIT**
- Detection fingerprint (Flock OUI set + DeFlock Joplin probe-request IE signature)
  is from flock-you and the **DeFlock** research it cites: <https://deflock.me>
- This port is **passive / receive-only** — it listens in Wi-Fi promiscuous mode and
  never transmits.

## This edition's changes
- Flat instrument menus with compact text, solid selection and readable secondary labels.
- Static SHARKDIV logo boot screen without background texture or a glitch reel.
- Original passive Find My validation, RF occupancy measurements and Remote ID transport parsing.
- ESP Web Tools 10.1.1 alternate installer / console (Apache-2.0), loaded from its public package; the original esptool-js board-power path remains available.
- Integration of Flock Hunter as a new WiFi-menu tool.

## Libraries
- **TFT_eSPI** by Bodmer — <https://github.com/Bodmer/TFT_eSPI> (MIT/FreeBSD).
- **ESP32 Arduino core** by Espressif — <https://github.com/espressif/arduino-esp32>.

## Referenced integrations
See [docs/INTEGRATIONS.md](docs/INTEGRATIONS.md) for the status of each requested upstream project, code provenance, dependency licenses and limitations. No code was copied from the unlicensed AirTag Scanner or Universal-RC repositories.
