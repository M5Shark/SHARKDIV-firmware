// Passive BLE evidence receiver; no connections, GATT reads, or scan requests.
#include "flock_ble.h"
#include "flock_config.h"
#include "config.h"
#include "FlockSignatures.h"
#include "BleHostOperation.h"
#include <cstring>

namespace flock_ble {
static Hit s_hits[kMaxHits];
static int s_count = 0;
static bool s_running = false;
static bool s_ownsScan = false;
static bool s_accept = false;
static uint32_t s_advertisements = 0;
static int s_error = 0;
static BLEScan* s_scan = nullptr;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static void upsert(const std::string& name, const std::string& mac, int rssi, uint8_t evidence) {
  if (rssi < flk::kRssiPossible) return;
  portENTER_CRITICAL(&s_lock);
  if (!s_accept) { portEXIT_CRITICAL(&s_lock); return; }
  const uint32_t now = millis();
  int slot = -1;
  for (int i = 0; i < s_count; ++i)
    if (!strcmp(s_hits[i].mac, mac.c_str())) { slot = i; break; }
  if (slot < 0) {
    if (s_count < kMaxHits) slot = s_count++;
    else {
      slot = 0;
      for (int i = 1; i < kMaxHits; ++i)
        if (uint32_t(now - s_hits[i].lastMs) > uint32_t(now - s_hits[slot].lastMs)) slot = i;
    }
    s_hits[slot] = {};
    strncpy(s_hits[slot].mac, mac.c_str(), sizeof(s_hits[slot].mac) - 1);
  }
  Hit& hit = s_hits[slot];
  if (!name.empty()) {
    strncpy(hit.name, name.c_str(), sizeof(hit.name) - 1);
    hit.name[sizeof(hit.name) - 1] = 0;
  } else if (!hit.name[0]) {
    const char* fallback = evidence & FlockSignatures::ByService ? "Service match" : "Vendor 09C8";
    strncpy(hit.name, fallback, sizeof(hit.name) - 1);
  }
  hit.rssi = (int8_t)rssi;
  hit.evidence |= evidence;
  hit.lastMs = now;
  portEXIT_CRITICAL(&s_lock);
}

class Cb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* device) override {
    if (!device) return;
    portENTER_CRITICAL(&s_lock);
    const bool accept = s_accept;
    if (accept && s_advertisements != UINT32_MAX) ++s_advertisements;
    portEXIT_CRITICAL(&s_lock);
    if (!accept) return;
    const uint8_t evidence = FlockSignatures::bleAdvertisementEvidence(
        device->getPayload(), device->getPayloadLength());
    if (evidence) upsert(device->getName(), device->getAddress().toString(), device->getRSSI(), evidence);
  }
};
static Cb s_cb;

void begin() {
  stopWindow();
  portENTER_CRITICAL(&s_lock);
  // Retain recent observations across launcher/view changes; snapshot expires them.
  const uint32_t now = millis();
  int kept = 0;
  for (int i = 0; i < s_count; ++i)
    if (uint32_t(now - s_hits[i].lastMs) < flk::kHitTtlMs) s_hits[kept++] = s_hits[i];
  s_count = kept;
  portEXIT_CRITICAL(&s_lock);
  s_error = 0;
}

bool startWindow() {
  if (running()) return true;
  if (s_ownsScan) stopWindow();
  s_error = 0;
  if (!ensureBleStackReady()) { s_error = -2; return false; }
  struct Result { BLEScan* scan; bool acquired; bool success; } result = {nullptr, false, false};
  const bool executed = runBleHostOperation([](void* context) {
    Result& result = *static_cast<Result*>(context);
    BLEScan* scan = BLEDevice::getScan();
    result.scan = scan;
    if (!scan || !scan->stop()) return;
    result.acquired = true;
    scan->setAdvertisedDeviceCallbacks(nullptr);
    scan->clearResults();
    scan->setAdvertisedDeviceCallbacks(&s_cb, true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(100);
    scan->setDuplicateFilter(false);
    scan->setMaxResults(0);
    portENTER_CRITICAL(&s_lock);
    s_accept = true;
    portEXIT_CRITICAL(&s_lock);
    result.success = scan->start((flk::kBleWindowMs + 999) / 1000, nullptr, false);
  }, &result);
  s_scan = result.scan;
  s_ownsScan = executed && result.acquired;
  s_running = executed && result.success;
  if (!s_running) { stopWindow(); s_error = -1; }
  return s_running;
}

void stopWindow() {
  portENTER_CRITICAL(&s_lock);
  s_accept = false;
  portEXIT_CRITICAL(&s_lock);
  s_running = false;
  if (!s_ownsScan || !s_scan) return;
  bool stopped = false;
  const bool executed = runBleHostOperation([](void* context) {
    bool& stopped = *static_cast<bool*>(context);
    if (!s_scan->stop()) return;
    s_scan->setAdvertisedDeviceCallbacks(nullptr);
    s_scan->clearResults();
    s_scan->setMaxResults(0xFF);
    stopped = true;
  }, &stopped);
  if (executed && stopped) { s_ownsScan = false; s_scan = nullptr; }
  else s_error = -3;
}

int snapshot(Hit* out, int max) {
  if (!out || max <= 0) return 0;
  Hit tmp[kMaxHits];
  portENTER_CRITICAL(&s_lock);
  const uint32_t now = millis();
  int count = 0;
  for (int i = 0; i < s_count; ++i)
    if (uint32_t(now - s_hits[i].lastMs) < flk::kHitTtlMs) s_hits[count++] = s_hits[i];
  s_count = count;
  memcpy(tmp, s_hits, sizeof(Hit) * count);
  portEXIT_CRITICAL(&s_lock);
  for (int i = 1; i < count; ++i) {
    const Hit hit = tmp[i];
    int j = i;
    while (j > 0 && tmp[j - 1].rssi < hit.rssi) { tmp[j] = tmp[j - 1]; --j; }
    tmp[j] = hit;
  }
  const int copied = count < max ? count : max;
  memcpy(out, tmp, sizeof(Hit) * copied);
  return copied;
}
int matches() { Hit hits[kMaxHits]; return snapshot(hits, kMaxHits); }
int bestRssi() { Hit hits[kMaxHits]; return snapshot(hits, kMaxHits) ? hits[0].rssi : -128; }
void poll() { if (s_running && s_scan && !s_scan->isScanning()) stopWindow(); }
bool running() { return s_running && s_scan && s_scan->isScanning(); }
int lastError() { return s_error; }
uint32_t advertisementsSeen() {
  portENTER_CRITICAL(&s_lock);
  const uint32_t count = s_advertisements;
  portEXIT_CRITICAL(&s_lock);
  return count;
}
}  // namespace flock_ble
