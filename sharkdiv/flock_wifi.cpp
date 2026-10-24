// Receive-only Wi-Fi AP and probe observations. Signatures are candidate evidence.
#include "DetectionAlerts.h"
#include "flock_wifi.h"
#include "flock_config.h"
#include "FlockSignatures.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <cstring>
#include <cstdio>

namespace flock_wifi {
static Hit s_hits[kMaxHits];
static int s_hitCount = 0;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_matchSound = true;

void setMatchSound(bool enabled) {
  portENTER_CRITICAL(&s_lock);
  s_matchSound = enabled;
  portEXIT_CRITICAL(&s_lock);
}
static bool s_scanning = false;
static bool s_probing = false;
static bool s_acceptProbes = false;
static bool s_hop = true;
static uint8_t s_channelIndex = 0;
static const uint8_t kChannels[] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
static uint32_t s_lastHop = 0;
static uint32_t s_scanStarted = 0;
static uint32_t s_probeFrames = 0;
static int s_apsSeen = 0;
static int s_error = 0;
static uint8_t s_extraOuis[16][3];
static int s_extraOuiCount = 0;

void addOui(const char* oui) {
  unsigned a, b, c;
  char tail;
  if (!oui || strlen(oui) != 8 || sscanf(oui, "%2x:%2x:%2x%c", &a, &b, &c, &tail) != 3 ||
      a > 255 || b > 255 || c > 255 || (a & 1)) return;
  const uint8_t prefix[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c};
  portENTER_CRITICAL(&s_lock);
  bool exists = false;
  for (int i = 0; i < s_extraOuiCount; ++i)
    exists |= memcmp(s_extraOuis[i], prefix, 3) == 0;
  if (!exists && s_extraOuiCount < 16) memcpy(s_extraOuis[s_extraOuiCount++], prefix, 3);
  portEXIT_CRITICAL(&s_lock);
}

static bool ouiMatches(const uint8_t* mac) {
  if (!mac || (mac[0] & 1)) return false;
  if (FlockSignatures::wifiOui(mac)) return true;
  bool matched = false;
  portENTER_CRITICAL(&s_lock);
  for (int i = 0; i < s_extraOuiCount; ++i)
    matched |= memcmp(mac, s_extraOuis[i], 3) == 0;
  portEXIT_CRITICAL(&s_lock);
  return matched;
}

static void upsert(const uint8_t* mac, const char* ssid, int8_t rssi,
                   bool bySsid, bool byOui, bool byProbe, bool byFingerprint) {
  if (!mac || rssi < flk::kRssiPossible) return;
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  portENTER_CRITICAL(&s_lock);
  if (byProbe && !s_acceptProbes) { portEXIT_CRITICAL(&s_lock); return; }
  const uint32_t now = millis();
  int slot = -1;
  for (int i = 0; i < s_hitCount; ++i)
    if (!strcmp(s_hits[i].mac, macStr)) { slot = i; break; }
  if (slot < 0) {
    if (s_hitCount < kMaxHits) slot = s_hitCount++;
    else {
      slot = 0;
      for (int i = 1; i < kMaxHits; ++i)
        if (uint32_t(now - s_hits[i].lastMs) > uint32_t(now - s_hits[slot].lastMs)) slot = i;
    }
    s_hits[slot] = {};
    memcpy(s_hits[slot].mac, macStr, sizeof(macStr));
  }
  Hit& h = s_hits[slot];
  h.rssi = rssi;
  h.lastMs = now;
  h.bySsid |= bySsid;
  h.byOui |= byOui;
  h.byProbe |= byProbe;
  h.byFingerprint |= byFingerprint;
  if (ssid && ssid[0]) { strncpy(h.ssid, ssid, 32); h.ssid[32] = 0; }
  const bool sound = s_matchSound;
  portEXIT_CRITICAL(&s_lock);
  if (sound) DetectionAlerts::notify(DetectionAlerts::Source::Flock,
                                     DetectionAlerts::hash(mac, 6));
}

static void pruneLocked(uint32_t now) {
  int kept = 0;
  for (int i = 0; i < s_hitCount; ++i)
    if (uint32_t(now - s_hits[i].lastMs) < flk::kHitTtlMs) s_hits[kept++] = s_hits[i];
  s_hitCount = kept;
}

void begin() {
  stop();
  portENTER_CRITICAL(&s_lock);
  // Retain recent observations when opening another view of the same tool.
  pruneLocked(millis());
  s_extraOuiCount = 0;
  portEXIT_CRITICAL(&s_lock);
  s_error = 0;
}

bool startScan() {
  if (s_scanning) return true;  // Never delete a scan that is still in progress.
  stopProbeWindow();
  s_error = 0;
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) { s_error = ESP_ERR_WIFI_STATE; return false; }
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_MODE_STA)) { s_error = ESP_FAIL; return false; }
  WiFi.disconnect(false, false);  // Keep STA enabled; preserve saved credentials.
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true, true, true, 180);  // async, hidden, PASSIVE
  s_scanning = result == WIFI_SCAN_RUNNING || result >= 0;
  s_scanStarted = millis();
  s_apsSeen = 0;
  if (!s_scanning) s_error = result;
  return s_scanning;
}

bool pollScan(int* matchedCount) {
  if (s_scanning) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING && uint32_t(millis() - s_scanStarted) < 6500) return false;
    if (n >= 0) {
      s_apsSeen = n;
      for (int i = 0; i < n; ++i) {
        const String name = WiFi.SSID(i);
        const uint8_t* mac = WiFi.BSSID(i);
        if (!mac) continue;
        const bool bySsid = FlockSignatures::ssid(name.c_str());
        const bool byOui = ouiMatches(mac);
        if (bySsid || byOui) upsert(mac, name.c_str(), WiFi.RSSI(i), bySsid, byOui, false, false);
      }
    } else {
      s_error = n == WIFI_SCAN_RUNNING ? ESP_ERR_TIMEOUT : n;
      esp_wifi_scan_stop();
    }
    WiFi.scanDelete();
    s_scanning = false;
  }
  if (matchedCount) *matchedCount = hitCount();
  return true;
}

static void probeCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const auto evidence = FlockSignatures::probeFrame(pkt->payload, pkt->rx_ctrl.sig_len, true);
  if (!evidence.valid) return;
  portENTER_CRITICAL(&s_lock);
  const bool accept = s_acceptProbes;
  if (accept && s_probeFrames != UINT32_MAX) ++s_probeFrames;
  portEXIT_CRITICAL(&s_lock);
  if (!accept || !evidence.wildcard) return;
  const bool byOui = ouiMatches(pkt->payload + 10);
  if (!byOui && !evidence.fingerprint) return;
  upsert(pkt->payload + 10, "", pkt->rx_ctrl.rssi, false, byOui, true, evidence.fingerprint);
}

bool startProbeWindow(bool hop) {
  if (s_probing) { s_hop = hop; return true; }
  if (s_scanning || WiFi.scanComplete() == WIFI_SCAN_RUNNING) { s_error = ESP_ERR_WIFI_STATE; return false; }
  s_error = 0;
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_MODE_STA)) { s_error = ESP_FAIL; return false; }
  WiFi.disconnect(false, false);
  s_hop = hop;
  s_channelIndex = 0;
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_err_t error = esp_wifi_set_promiscuous(false);
  if (error == ESP_OK) error = esp_wifi_set_promiscuous_filter(&filter);
  if (error == ESP_OK) error = esp_wifi_set_channel(kChannels[0], WIFI_SECOND_CHAN_NONE);
  if (error == ESP_OK) error = esp_wifi_set_promiscuous_rx_cb(&probeCb);
  portENTER_CRITICAL(&s_lock);
  s_acceptProbes = error == ESP_OK;
  portEXIT_CRITICAL(&s_lock);
  if (error == ESP_OK) error = esp_wifi_set_promiscuous(true);
  s_probing = error == ESP_OK;
  s_lastHop = millis();
  if (!s_probing) {
    s_error = error;
    portENTER_CRITICAL(&s_lock);
    s_acceptProbes = false;
    portEXIT_CRITICAL(&s_lock);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }
  return s_probing;
}

void stopProbeWindow() {
  portENTER_CRITICAL(&s_lock);
  s_acceptProbes = false;
  portEXIT_CRITICAL(&s_lock);
  if (s_probing) {
    const esp_err_t error = esp_wifi_set_promiscuous(false);
    if (error != ESP_OK) s_error = error;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }
  s_probing = false;
}

void stop() {
  stopProbeWindow();
  if (s_scanning) {
    esp_wifi_scan_stop();
    WiFi.scanDelete();
    s_scanning = false;
  }
}

void pollProbes() {
  if (!s_probing || !s_hop || uint32_t(millis() - s_lastHop) < 250) return;
  const uint8_t next = (s_channelIndex + 1) % sizeof(kChannels);
  const esp_err_t error = esp_wifi_set_channel(kChannels[next], WIFI_SECOND_CHAN_NONE);
  if (error == ESP_OK) s_channelIndex = next;
  else s_error = error;
  s_lastHop = millis();
}

int snapshot(Hit* out, int max) {
  if (!out || max <= 0) return 0;
  Hit tmp[kMaxHits];
  portENTER_CRITICAL(&s_lock);
  pruneLocked(millis());
  const int n = s_hitCount;
  memcpy(tmp, s_hits, sizeof(Hit) * n);
  portEXIT_CRITICAL(&s_lock);
  for (int i = 1; i < n; ++i) {
    const Hit hit = tmp[i];
    int j = i;
    while (j > 0 && tmp[j - 1].rssi < hit.rssi) { tmp[j] = tmp[j - 1]; --j; }
    tmp[j] = hit;
  }
  const int count = n < max ? n : max;
  memcpy(out, tmp, sizeof(Hit) * count);
  return count;
}
int hitCount() {
  portENTER_CRITICAL(&s_lock);
  pruneLocked(millis());
  const int count = s_hitCount;
  portEXIT_CRITICAL(&s_lock);
  return count;
}
int bestRssi() { Hit hits[kMaxHits]; return snapshot(hits, kMaxHits) ? hits[0].rssi : -128; }
bool anyConfirmed() {
  Hit hits[kMaxHits];
  const int count = snapshot(hits, kMaxHits);
  for (int i = 0; i < count; ++i) if (hits[i].byFingerprint) return true;
  return false;  // Legacy API: only a full fingerprint, still not identity proof.
}
bool scanning() { return s_scanning; }
int lastError() { return s_error; }
int apsSeen() { return s_apsSeen; }
uint8_t channel() { return kChannels[s_channelIndex]; }
uint32_t probeFramesSeen() {
  portENTER_CRITICAL(&s_lock);
  const uint32_t count = s_probeFrames;
  portEXIT_CRITICAL(&s_lock);
  return count;
}
}  // namespace flock_wifi
