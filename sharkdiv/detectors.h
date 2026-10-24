#pragma once
// detectors.h — unified passive fingerprint detector engine. One engine,
// many signature sets (BLE name/UUID/company ID + WiFi SSID/OUI). All
// detection is receive-only; nothing transmits.
#include <Arduino.h>

namespace Detectors {
  void runLauncher();          // list of detectors (from More -> Detectors)
}
