// flock_wifi.cpp — see header. No transmission of any kind.
#include "flock_wifi.h"
#include "flock_config.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include <cstring>
#include <cstdio>

namespace flock_wifi {

static constexpr int kMaxHits = 12;
static Hit s_hits[kMaxHits];
static int s_hitCount = 0;
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
  for (int i = 0; i < s_hitCount; i++) {
    if (!strcmp(s_hits[i].mac, macStr)) {
      s_hits[i].rssi = rssi;
      s_hits[i].lastMs = millis();
      s_hits[i].bySsid |= bySsid;
      s_hits[i].byOui |= byOui;
      s_hits[i].byProbe |= byProbe;
      return;
    }
  }
  if (s_hitCount >= kMaxHits) return;
  Hit& h = s_hits[s_hitCount++];
  strncpy(h.mac, macStr, 17); h.mac[17] = 0;
  strncpy(h.ssid, ssid ? ssid : "", 32); h.ssid[32] = 0;
  h.rssi = rssi;
  h.bySsid = bySsid; h.byOui = byOui; h.byProbe = byProbe;
  h.lastMs = millis();
}

void begin() {
  s_hitCount = 0;
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

const Hit* hits() { return s_hits; }
int hitCount() { return s_hitCount; }

int bestRssi() {
  int best = -128;
  for (int i = 0; i < s_hitCount; i++) {
    if (s_hits[i].rssi > best) best = s_hits[i].rssi;
  }
  return best;
}

bool anyConfirmed() {
  for (int i = 0; i < s_hitCount; i++) {
    if (s_hits[i].bySsid || (s_hits[i].byOui && s_hits[i].byProbe)) return true;
  }
  return false;
}

}  // namespace flock_wifi
