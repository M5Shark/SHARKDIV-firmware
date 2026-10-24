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
- Portrait pixel-skull intro adapted from the supplied visual reference, followed by the user's SHARKDIV logo.
- Original passive Find My validation, RF occupancy measurements and Remote ID transport parsing.
- ESP Web Tools 10.1.1 alternate installer / console (Apache-2.0), loaded from its public package; the original esptool-js board-power path remains available.
- Integration of Flock Hunter as a new WiFi-menu tool.

## Libraries
- **TFT_eSPI** by Bodmer — <https://github.com/Bodmer/TFT_eSPI> (MIT/FreeBSD).
- **ESP32 Arduino core** by Espressif — <https://github.com/espressif/arduino-esp32>.

## Startup artwork
- The skull poses are converted from the user's `skull-glitch/assets/skull-sprite.png`, a Watch Dogs 2 / DedSec visual reference (Ubisoft). The firmware adapts its framing and timing for the portrait display and follows it with the user's M5 SHARK logo.
- The supplied artwork retains its respective ownership; the project's source-code license does not relicense third-party artwork. The PNG conversion and source hash are recorded in `tools/pack_boot_sprite.py` and `sharkdiv/SkullBootFrames.h`.

## Referenced integrations
See [docs/INTEGRATIONS.md](docs/INTEGRATIONS.md) for the status of each requested upstream project, code provenance, dependency licenses and limitations. No code was copied from the unlicensed AirTag Scanner or Universal-RC repositories.
