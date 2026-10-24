// Passive Flock candidate receiver. Published evidence is not camera identity.
#include "DetectionAlerts.h"
#include "flock_hunter.h"
#include "config.h"
#include "Touchscreen.h"
#include "utils.h"
#include <WiFi.h>
#include <Preferences.h>
#include "esp_wifi.h"

namespace FlockHunter {
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Detection s_det[kMaxDetections];
static uint8_t s_detCount = 0;
static uint32_t s_probeCount = 0;
static uint32_t s_matchCount = 0;
static bool s_accept = false;
static bool s_matchSound = true;
static bool s_ready = false;
static bool s_dirty = true;
static uint8_t s_chanIdx = 0;
static uint32_t s_lastHop = 0;
static uint32_t s_lastRedraw = 0;
static int s_error = 0;

static int contentBottom() { return featureHasTouchNavBar() ? touchNavContentBottomY() : tft.height(); }

static void recordHit(const uint8_t* mac, int8_t rssi, uint8_t channel,
                      const FlockSignatures::ProbeEvidence& evidence) {
  portENTER_CRITICAL(&s_mux);
  if (!s_accept) { portEXIT_CRITICAL(&s_mux); return; }
  const uint32_t now = millis();
  if (s_matchCount != UINT32_MAX) ++s_matchCount;
  int slot = -1;
  for (uint8_t i = 0; i < s_detCount; ++i)
    if (memcmp(s_det[i].mac, mac, 6) == 0) { slot = i; break; }
  if (slot < 0) {
    if (s_detCount < kMaxDetections) slot = s_detCount++;
    else {
      slot = 0;
      for (uint8_t i = 1; i < s_detCount; ++i)
        if (uint32_t(now - s_det[i].lastSeen) > uint32_t(now - s_det[slot].lastSeen)) slot = i;
    }
    s_det[slot] = {};
    memcpy(s_det[slot].mac, mac, 6);
    s_det[slot].firstSeen = now;
  }
  Detection& hit = s_det[slot];
  hit.rssi = rssi;
  hit.channel = channel;
  hit.lastSeen = now;
  hit.byOui |= evidence.oui;
  hit.fingerprint |= evidence.fingerprint;
  if (hit.count != UINT16_MAX) ++hit.count;
  s_dirty = true;
  const bool sound = s_matchSound;
  portEXIT_CRITICAL(&s_mux);
  if (sound) DetectionAlerts::notify(DetectionAlerts::Source::Flock,
                                     DetectionAlerts::hash(mac, 6));
}

static void promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!buf || type != WIFI_PKT_MGMT) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  const auto evidence = FlockSignatures::probeFrame(pkt->payload, pkt->rx_ctrl.sig_len, true);
  if (!evidence.valid) return;
  portENTER_CRITICAL(&s_mux);
  const bool accept = s_accept;
  if (accept && s_probeCount != UINT32_MAX) ++s_probeCount;
  portEXIT_CRITICAL(&s_mux);
  if (!accept || pkt->rx_ctrl.rssi < kRssiMin || !evidence.wildcard ||
      (!evidence.oui && !evidence.fingerprint)) return;
  recordHit(pkt->payload + 10, pkt->rx_ctrl.rssi, pkt->rx_ctrl.channel, evidence);
}

static void drawFrame() {
  featureClearContent(FEATURE_BG);
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(10, 25);
  tft.print("FLOCK / PROBE MONITOR");
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(10, 40);
  tft.print("Passive | channels 1-11 | 2 min");
  tft.drawFastHLine(8, 53, tft.width() - 16, UI_LINE);
  redrawTouchButtonBar();
}

static void drawStatus(uint8_t count, uint32_t probes, uint32_t matches) {
  tft.fillRect(8, 58, tft.width() - 16, 33, FEATURE_BG);
  tft.setTextSize(1);
  tft.setTextColor(s_error ? UI_WARN : UI_TEXT, FEATURE_BG);
  tft.setCursor(10, 58);
  if (s_error) tft.printf("Radio error: %d", s_error);
  else tft.printf("Ch %u | %u candidate addresses", kChannels[s_chanIdx], count);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  tft.setCursor(10, 70);
  tft.printf("Probes %lu | matched %lu", (unsigned long)probes, (unsigned long)matches);
  tft.setCursor(10, 82);
  tft.print("Signatures do not confirm a camera");
}

static void drawList() {
  Detection snapshot[kMaxDetections];
  portENTER_CRITICAL(&s_mux);
  const uint32_t now = millis();
  uint8_t count = 0;
  for (uint8_t i = 0; i < s_detCount; ++i)
    if (uint32_t(now - s_det[i].lastSeen) < kHitTtlMs) s_det[count++] = s_det[i];
  s_detCount = count;
  memcpy(snapshot, s_det, sizeof(Detection) * count);
  const uint32_t probes = s_probeCount;
  const uint32_t matches = s_matchCount;
  s_dirty = false;
  portEXIT_CRITICAL(&s_mux);
  for (uint8_t i = 1; i < count; ++i) {
    const Detection hit = snapshot[i];
    int j = i;
    while (j > 0 && snapshot[j - 1].rssi < hit.rssi) { snapshot[j] = snapshot[j - 1]; --j; }
    snapshot[j] = hit;
  }
  drawStatus(count, probes, matches);
  const int top = 99;
  const int rowHeight = 27;
  const int maxRows = (contentBottom() - top - 15) / rowHeight;
  tft.fillRect(8, top, tft.width() - 16, contentBottom() - top, FEATURE_BG);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
  if (!count) {
    tft.setCursor(10, top + 8);
    tft.print(probes ? "Receiving probes; no matches yet" : "Waiting for nearby probe traffic");
    return;
  }
  const int shown = count < maxRows ? count : maxRows;
  for (int i = 0; i < shown; ++i) {
    const Detection& hit = snapshot[i];
    tft.setCursor(10, top + i * rowHeight);
    tft.setTextColor(hit.fingerprint ? UI_WARN : UI_TEXT, FEATURE_BG);
    tft.printf("%02X:%02X:%02X:%02X:%02X:%02X", hit.mac[0], hit.mac[1], hit.mac[2],
               hit.mac[3], hit.mac[4], hit.mac[5]);
    tft.setCursor(10, top + i * rowHeight + 12);
    tft.setTextColor(UI_DIM_TEXT, FEATURE_BG);
    tft.printf("%s %ddBm ch%u %lus", hit.fingerprint ? "IE" : "OUI", hit.rssi,
               hit.channel, (unsigned long)((now - hit.lastSeen) / 1000));
  }
  if (count > shown) {
    tft.setCursor(10, contentBottom() - 12);
    tft.printf("Showing %d strongest of %u", shown, count);
  }
}

void flockSetup() {
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);
  flockStop();
  Preferences prefs;
  bool matchSound = true;
  if (prefs.begin("flock", true)) { matchSound = prefs.getBool("snd", true); prefs.end(); }
  portENTER_CRITICAL(&s_mux);
  s_matchSound = matchSound;
  s_detCount = 0;
  s_probeCount = 0;
  s_matchCount = 0;
  s_dirty = true;
  portEXIT_CRITICAL(&s_mux);
  s_chanIdx = 0;
  s_lastHop = millis();
  s_lastRedraw = 0;
  s_error = 0;
  WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_MODE_STA)) s_error = ESP_FAIL;
  // wifiOff=true disabled the very receiver we were about to use.
  WiFi.disconnect(false, false);
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  if (!s_error) s_error = esp_wifi_set_promiscuous(false);
  if (!s_error) s_error = esp_wifi_set_promiscuous_filter(&filter);
  if (!s_error) s_error = esp_wifi_set_channel(kChannels[0], WIFI_SECOND_CHAN_NONE);
  if (!s_error) s_error = esp_wifi_set_promiscuous_rx_cb(&promiscCb);
  portENTER_CRITICAL(&s_mux);
  s_accept = s_error == 0;
  portEXIT_CRITICAL(&s_mux);
  if (!s_error) s_error = esp_wifi_set_promiscuous(true);
  s_ready = s_error == 0;
  if (!s_ready) {
    portENTER_CRITICAL(&s_mux);
    s_accept = false;
    portEXIT_CRITICAL(&s_mux);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }
  drawFrame();
  drawList();
}

void flockLoop() {
  if (feature_exit_requested) return;
  const uint32_t now = millis();
  if (s_ready && uint32_t(now - s_lastHop) >= kChannelDwellMs) {
    const uint8_t next = (s_chanIdx + 1) % sizeof(kChannels);
    const esp_err_t error = esp_wifi_set_channel(kChannels[next], WIFI_SECOND_CHAN_NONE);
    if (error == ESP_OK) s_chanIdx = next;
    else s_error = error;
    s_lastHop = now;
  }
  if (uint32_t(now - s_lastRedraw) >= 250) { s_lastRedraw = now; drawList(); }
  updateStatusBar();
  maintainTouchNavBar();
  delay(5);
}

void flockStop() {
  portENTER_CRITICAL(&s_mux);
  s_accept = false;
  portEXIT_CRITICAL(&s_mux);
  if (s_ready) {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }
  s_ready = false;
}
}  // namespace FlockHunter
