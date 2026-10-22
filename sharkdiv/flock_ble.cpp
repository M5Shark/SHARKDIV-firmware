// flock_ble.cpp — passive (non-connecting, non-active-scan) BLE observer.
#include "flock_ble.h"
#include "flock_config.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <cstring>

namespace flock_ble {

static int s_matches = 0;
static int s_best = -128;
static bool s_running = false;

class Cb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* d) override {
    if (!d || !d->haveName()) return;
    const std::string n = d->getName();
    if (n.empty()) return;
    for (int i = 0; i < flk::kBleNameCount; i++) {
      if (n.find(flk::kBleNames[i]) != std::string::npos) {
        s_matches++;
        const int r = (int)d->getRSSI();
        if (r > s_best) s_best = r;
        return;
      }
    }
  }
};
static Cb s_cb;

void begin() {
  s_matches = 0;
  s_best = -128;
}

void startWindow() {
  ensureBleStackReady();
  BLEScan* scan = BLEDevice::getScan();
  scan->stop();
  scan->setAdvertisedDeviceCallbacks(&s_cb, true);
  scan->setActiveScan(false);        // PASSIVE: no scan requests sent
  scan->setInterval(100);
  scan->setWindow(100);
  scan->setDuplicateFilter(true);
  s_matches = 0;
  s_best = -128;
  // duration-limited, non-blocking overload
  scan->start(flk::kBleWindowMs / 1000, nullptr, false);
  s_running = true;
}

void stopWindow() {
  BLEScan* scan = BLEDevice::getScan();
  if (scan) scan->stop();
  s_running = false;
}

int matches() { return s_matches; }
void poll() {}
int bestRssi() { return s_best; }
bool running() { return s_running; }

}  // namespace flock_ble
