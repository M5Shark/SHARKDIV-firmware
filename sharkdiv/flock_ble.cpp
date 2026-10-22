// flock_ble.cpp — passive (non-connecting, non-active-scan) BLE observer.
// Matched devices are kept in a small table across scan windows and expire
// after flk::kHitTtlMs, so the UI shows what is around now.
#include "flock_ble.h"
#include "flock_config.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <cstring>

namespace flock_ble {

static Hit s_hits[kMaxHits];
static int s_count = 0;
static bool s_running = false;
// Written from the BLE host task, read from the UI loop.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void upsert(const std::string& name, const std::string& mac, int rssi) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&s_lock);
  int slot = -1;
  for (int i = 0; i < s_count; i++) {
    if (!strcmp(s_hits[i].mac, mac.c_str())) { slot = i; break; }
  }
  if (slot < 0) {
    if (s_count < kMaxHits) {
      slot = s_count++;
    } else {                     // table full: reuse the stalest entry
      slot = 0;
      for (int i = 1; i < kMaxHits; i++)
        if (s_hits[i].lastMs < s_hits[slot].lastMs) slot = i;
    }
    strncpy(s_hits[slot].mac, mac.c_str(), 17); s_hits[slot].mac[17] = 0;
  }
  strncpy(s_hits[slot].name, name.c_str(), 23); s_hits[slot].name[23] = 0;
  s_hits[slot].rssi = (int8_t)rssi;
  s_hits[slot].lastMs = now;
  portEXIT_CRITICAL(&s_lock);
}

class Cb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* d) override {
    if (!d || !d->haveName()) return;
    const std::string n = d->getName();
    if (n.empty()) return;
    for (int i = 0; i < flk::kBleNameCount; i++) {
      if (n.find(flk::kBleNames[i]) != std::string::npos) {
        upsert(n, d->getAddress().toString(), d->getRSSI());
        return;
      }
    }
  }
};
static Cb s_cb;

void begin() {
  portENTER_CRITICAL(&s_lock);
  s_count = 0;
  portEXIT_CRITICAL(&s_lock);
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
  // duration-limited, non-blocking overload
  scan->start(flk::kBleWindowMs / 1000, nullptr, false);
  s_running = true;
}

void stopWindow() {
  BLEScan* scan = BLEDevice::getScan();
  if (scan) scan->stop();
  s_running = false;
}

int snapshot(Hit* out, int max) {
  Hit tmp[kMaxHits];
  const uint32_t now = millis();
  portENTER_CRITICAL(&s_lock);
  int w = 0;
  for (int i = 0; i < s_count; i++) {
    if (now - s_hits[i].lastMs <= flk::kHitTtlMs) s_hits[w++] = s_hits[i];
  }
  s_count = w;
  memcpy(tmp, s_hits, sizeof(Hit) * w);
  portEXIT_CRITICAL(&s_lock);
  for (int i = 1; i < w; i++) {       // strongest first
    const Hit k = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j].rssi < k.rssi) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  const int c = w < max ? w : max;
  memcpy(out, tmp, sizeof(Hit) * c);
  return c;
}

int matches() {
  Hit tmp[kMaxHits];
  return snapshot(tmp, kMaxHits);
}

int bestRssi() {
  Hit tmp[kMaxHits];
  return snapshot(tmp, kMaxHits) > 0 ? tmp[0].rssi : -128;
}

void poll() {}
bool running() { return s_running; }

}  // namespace flock_ble
