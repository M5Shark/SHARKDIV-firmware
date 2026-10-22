// flock_wifi.cpp — see header. No transmission of any kind.
#include "flock_wifi.h"
#include "flock_config.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <cstring>
#include <cstdio>

namespace flock_wifi {

static Hit s_hits[kMaxHits];
static int s_hitCount = 0;
// The hit table is written from the Wi-Fi task and read from the UI loop.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_scanning = false;
static bool s_probing = false;

// SD-extendable OUI list (loaded by flock_menu at startup).
static char s_extraOuis[16][10];
static int s_extraOuiCount = 0;

void addOui(const char* oui) {
  if (s_extraOuiCount < 16) {
    strncpy(s_extraOuis[s_extraOuiCount], oui, 9);
    s_extraOuis[s_extraOuiCount][9] = 0;
    s_extraOuiCount++;
  }
}

static bool ssidMatches(const char* ssid) {
  for (int i = 0; i < flk::kSsidPatternCount; i++) {
    if (ssid && strstr(ssid, flk::kSsidPatterns[i])) return true;
    // Flock-<6 hex> pattern: prefix hit already covers it.
  }
  return false;
}

static bool ouiMatches(const uint8_t* mac) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
  for (int i = 0; i < flk::kOuiCount; i++) {
    if (!strcmp(buf, flk::kOuis[i])) return true;
  }
  for (int i = 0; i < s_extraOuiCount; i++) {
    if (!strcmp(buf, s_extraOuis[i])) return true;
  }
  return false;
}

static void upsert(const uint8_t* mac, const char* ssid, int8_t rssi,
                   bool bySsid, bool byOui, bool byProbe) {
  if (rssi < flk::kRssiPossible) return;
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  const uint32_t now = millis();
  portENTER_CRITICAL(&s_lock);
  int slot = -1;
  for (int i = 0; i < s_hitCount; i++) {
    if (!strcmp(s_hits[i].mac, macStr)) { slot = i; break; }
  }
  if (slot >= 0) {
    Hit& h = s_hits[slot];
    h.rssi = rssi;
    h.lastMs = now;
    h.bySsid |= bySsid;
    h.byOui |= byOui;
    h.byProbe |= byProbe;
    if (ssid && ssid[0] && !h.ssid[0]) { strncpy(h.ssid, ssid, 32); h.ssid[32] = 0; }
  } else {
    if (s_hitCount < kMaxHits) {
      slot = s_hitCount++;
    } else {                     // table full: reuse the stalest entry
      slot = 0;
      for (int i = 1; i < kMaxHits; i++)
        if (s_hits[i].lastMs < s_hits[slot].lastMs) slot = i;
    }
    Hit& h = s_hits[slot];
    strncpy(h.mac, macStr, 17); h.mac[17] = 0;
    strncpy(h.ssid, ssid ? ssid : "", 32); h.ssid[32] = 0;
    h.rssi = rssi;
    h.bySsid = bySsid; h.byOui = byOui; h.byProbe = byProbe;
    h.lastMs = now;
  }
  portEXIT_CRITICAL(&s_lock);
}

// Drop hits not seen for kHitTtlMs. Call with s_lock held.
static void pruneLocked(uint32_t now) {
  int w = 0;
  for (int i = 0; i < s_hitCount; i++) {
    if (now - s_hits[i].lastMs <= flk::kHitTtlMs) s_hits[w++] = s_hits[i];
  }
  s_hitCount = w;
}

void begin() {
  portENTER_CRITICAL(&s_lock);
  s_hitCount = 0;
  portEXIT_CRITICAL(&s_lock);
  s_extraOuiCount = 0;
}

void startScan() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true /*async*/);
  s_scanning = true;
}

bool pollScan(int* matchedCount) {
  if (!s_scanning) { if (matchedCount) *matchedCount = s_hitCount; return true; }
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return false;
  if (n >= 0) {
    for (int i = 0; i < n; i++) {
      const String ssid = WiFi.SSID(i);
      const uint8_t* bssid = WiFi.BSSID(i);
      const int8_t rssi = WiFi.RSSI(i);
      const bool bySsid = ssidMatches(ssid.c_str());
      const bool byOui = bssid ? ouiMatches(bssid) : false;
      if (bySsid || byOui) upsert(bssid, ssid.c_str(), rssi, bySsid, byOui, false);
    }
    WiFi.scanDelete();
  }
  s_scanning = false;
  if (matchedCount) *matchedCount = s_hitCount;
  return true;
}

// ── probe sniffer: probe REQUEST frames with an empty SSID ──
static void IRAM_ATTR probeCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 26) return;   // too short to hold the fields read below
  const uint8_t* f = pkt->payload;
  // frame control little-endian: type=sub>>2? use explicit bits.
  const uint8_t fc0 = f[0];
  const uint8_t subtype = (f[0] & 0xF0) >> 4;
  const uint8_t ftype = fc0 & 0x0F;
  if (ftype != 0x00 || subtype != 0x04) return;      // mgmt / probe req
  const uint8_t* src = f + 10;                        // addr2
  if (!ouiMatches(src)) return;
  // tagged params start at 24+fixed? probe req has no body: hdr 24 then SSID IE
  const uint8_t* ies = f + 24;
  const int ssidLen = ies[1];
  if (ssidLen != 0) return;                           // only empty SSID
  upsert(src, "", (int8_t)pkt->rx_ctrl.rssi, false, true, true);
}

void startProbeWindow() {
  if (s_probing) return;
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&probeCb);
  esp_wifi_set_promiscuous(true);
  s_probing = true;
}

void stopProbeWindow() {
  if (!s_probing) return;
  esp_wifi_set_promiscuous(false);
  s_probing = false;
}

void pollProbes() {}   // callback-driven

int snapshot(Hit* out, int max) {
  Hit tmp[kMaxHits];
  portENTER_CRITICAL(&s_lock);
  pruneLocked(millis());
  const int n = s_hitCount;
  memcpy(tmp, s_hits, sizeof(Hit) * n);
  portEXIT_CRITICAL(&s_lock);
  // strongest first (insertion sort; n <= 12)
  for (int i = 1; i < n; i++) {
    const Hit k = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j].rssi < k.rssi) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  const int c = n < max ? n : max;
  memcpy(out, tmp, sizeof(Hit) * c);
  return c;
}

int hitCount() {
  portENTER_CRITICAL(&s_lock);
  pruneLocked(millis());
  const int n = s_hitCount;
  portEXIT_CRITICAL(&s_lock);
  return n;
}

int bestRssi() {
  Hit tmp[kMaxHits];
  const int n = snapshot(tmp, kMaxHits);
  return n > 0 ? tmp[0].rssi : -128;
}

bool anyConfirmed() {
  Hit tmp[kMaxHits];
  const int n = snapshot(tmp, kMaxHits);
  for (int i = 0; i < n; i++) {
    if (tmp[i].bySsid || (tmp[i].byOui && tmp[i].byProbe)) return true;
  }
  return false;
}

}  // namespace flock_wifi
