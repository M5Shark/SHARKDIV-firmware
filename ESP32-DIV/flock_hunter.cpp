// flock_hunter.cpp — passive Flock Safety camera detector for ESP32-DIV.
//
// PASSIVE ONLY. Puts the radio in WiFi promiscuous (monitor) mode, hops the
// 2.4GHz channels Flock cameras probe on, and lists any frame that matches the
// Flock probe-request signature (see flock_hunter.h). It never transmits,
// authenticates, or starts an AP — it only listens and draws.
//
// Ported from colonelpanichacks/flock-you (MIT) + the DeFlock research it cites.
//
// Self-contained: depends only on the global `tft`, <WiFi.h>/<esp_wifi.h>, and
// flock_hunter.h. The sketch drives it exactly like every other WiFi feature:
//   flockSetup()  on entry, flockLoop() in the while-loop, flockStop() on exit.

#include "flock_hunter.h"
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "esp_wifi.h"

extern TFT_eSPI tft;

namespace FlockHunter {

// ── on-screen palette (green-on-black, matches the hacker theme) ──
static const uint16_t COL_BG   = 0x0000;  // black
static const uint16_t COL_GRN  = 0x07E0;  // bright green
static const uint16_t COL_DIM  = 0x03E0;  // dim green
static const uint16_t COL_HOT  = 0xFFE0;  // yellow highlight for a fresh hit

// ── shared detection state (written in the RX callback, read in the loop) ──
static portMUX_TYPE   s_mux = portMUX_INITIALIZER_UNLOCKED;
static Detection      s_det[kMaxDetections];
static volatile uint8_t  s_detCount   = 0;
static volatile uint32_t s_frameCount = 0;   // total Flock frames seen
static volatile bool     s_dirty      = true; // list needs a redraw

// ── channel hopping ──
static uint8_t  s_chanIdx    = 0;
static uint32_t s_lastHop    = 0;
static uint32_t s_lastRedraw = 0;

// Insert or refresh a detection. Runs in the WiFi task (callback) context.
static void recordHit(const uint8_t* mac, int8_t rssi, uint8_t channel) {
  portENTER_CRITICAL(&s_mux);
  s_frameCount++;
  int slot = -1;
  for (uint8_t i = 0; i < s_detCount; i++) {
    if (memcmp(s_det[i].mac, mac, 6) == 0) { slot = i; break; }
  }
  uint32_t now = millis();
  if (slot < 0) {
    if (s_detCount < kMaxDetections) {
      slot = s_detCount++;
      memcpy(s_det[slot].mac, mac, 6);
      s_det[slot].count     = 0;
      s_det[slot].firstSeen = now;
      s_dirty = true;
    }
  }
  if (slot >= 0) {
    Detection& d = s_det[slot];
    d.rssi    = rssi;
    d.channel = channel;
    d.lastSeen = now;
    if (d.count < 0xFFFF) d.count++;
    s_dirty = true;
  }
  portEXIT_CRITICAL(&s_mux);
}

// Promiscuous RX callback: gate every management frame through isFlockProbe().
static void promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  int len = pkt->rx_ctrl.sig_len;
  if (len <= (int)sizeof(MacHdr)) return;
  int8_t rssi = pkt->rx_ctrl.rssi;
  if (rssi < kRssiMin) return;

  const MacHdr* hdr = (const MacHdr*)pkt->payload;
  const uint8_t* body = pkt->payload + sizeof(MacHdr);
  int bodyLen = len - sizeof(MacHdr);

  if (isFlockProbe(hdr, body, bodyLen)) {
    recordHit(hdr->addr2, rssi, pkt->rx_ctrl.channel);
  }
}

// ── drawing ──
static void drawFrame() {
  tft.fillScreen(COL_BG);
  tft.drawRect(2, 2, tft.width() - 4, tft.height() - 4, COL_DIM);
  tft.setTextFont(1);
  tft.setTextColor(COL_GRN, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print(">FLOCK HUNTER");
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setCursor(10, 32);
  tft.print("passive scan  ch 1/6/11  SELECT=exit");
  tft.drawFastHLine(8, 46, tft.width() - 16, COL_DIM);
}

static void drawStatus() {
  // One status line: current channel + unique hits + frames seen.
  char line[48];
  snprintf(line, sizeof(line), "ch %-2u  hits:%-2u  frames:%lu   ",
           (unsigned)kChannels[s_chanIdx], (unsigned)s_detCount,
           (unsigned long)s_frameCount);
  tft.setTextSize(1);
  tft.setTextColor(COL_GRN, COL_BG);
  tft.setCursor(10, 52);
  tft.print(line);
}

static void drawList() {
  // Snapshot the table under the lock, then render without holding it.
  Detection snap[kMaxDetections];
  uint8_t n;
  portENTER_CRITICAL(&s_mux);
  n = s_detCount;
  memcpy(snap, s_det, sizeof(Detection) * (n < kMaxDetections ? n : kMaxDetections));
  s_dirty = false;
  portEXIT_CRITICAL(&s_mux);

  const int top = 66;
  const int rowH = 24;
  const int maxRows = (tft.height() - top - 8) / rowH;  // ~10 rows on 240x320

  tft.fillRect(6, top, tft.width() - 12, tft.height() - top - 6, COL_BG);
  tft.setTextSize(1);

  if (n == 0) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(12, top + 8);
    tft.print("listening... no flock cameras yet");
    return;
  }

  uint32_t now = millis();
  int shown = (n < maxRows) ? n : maxRows;
  for (int i = 0; i < shown; i++) {
    const Detection& d = snap[i];
    bool fresh = (now - d.lastSeen) < 1500;   // seen in the last 1.5s
    int y = top + i * rowH;
    tft.setTextColor(fresh ? COL_HOT : COL_GRN, COL_BG);
    char mac[20];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    tft.setCursor(12, y);
    tft.print(mac);
    char meta[28];
    snprintf(meta, sizeof(meta), "rssi %d  ch%u  x%u",
             (int)d.rssi, (unsigned)d.channel, (unsigned)d.count);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(16, y + 11);
    tft.print(meta);
  }
  if (n > shown) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setCursor(12, top + shown * rowH);
    tft.print("...more (table full)");
  }
}

// ── entry points ──
void flockSetup() {
  portENTER_CRITICAL(&s_mux);
  s_detCount   = 0;
  s_frameCount = 0;
  s_dirty      = true;
  portEXIT_CRITICAL(&s_mux);
  s_chanIdx    = 0;
  s_lastHop    = millis();
  s_lastRedraw = 0;

  // Bring the radio up in a clean monitor state.
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(true, true);
  esp_wifi_set_promiscuous(false);

  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;  // management frames only
  esp_wifi_set_promiscuous_filter(&filter);
  esp_wifi_set_promiscuous_rx_cb(&promiscCb);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(kChannels[0], WIFI_SECOND_CHAN_NONE);

  drawFrame();
  drawStatus();
  drawList();
}

void flockLoop() {
  uint32_t now = millis();

  // Hop channels on a timer so we cover 1 / 6 / 11.
  if (now - s_lastHop >= kChannelDwellMs) {
    s_lastHop = now;
    s_chanIdx = (s_chanIdx + 1) % (sizeof(kChannels) / sizeof(kChannels[0]));
    esp_wifi_set_channel(kChannels[s_chanIdx], WIFI_SECOND_CHAN_NONE);
    drawStatus();
  }

  // Redraw the list when it changed, but no more than ~4x/sec to limit flicker.
  if (s_dirty && (now - s_lastRedraw) >= 250) {
    s_lastRedraw = now;
    drawStatus();
    drawList();
  }

  delay(5);
}

void flockStop() {
  esp_wifi_set_promiscuous_rx_cb(NULL);
  esp_wifi_set_promiscuous(false);
}

}  // namespace FlockHunter
