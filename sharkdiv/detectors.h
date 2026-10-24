#pragma once
// Passive broadcast candidates, not verified device identities. WiFi Cameras
// includes built-in Flock indicators; optional SD lists add custom vendor
// prefixes. The paged launcher exposes every detector on the physical display.
#include <Arduino.h>

namespace Detectors {
  void runLauncher();          // list of detectors (from More -> Detectors)
}
