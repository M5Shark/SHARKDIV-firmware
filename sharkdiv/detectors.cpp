// detectors.cpp — see header. Signature-driven detectors share one engine:
// passive BLE observer scan + WiFi async scan (+ OUI from BSSIDs), hits
// scored by matched evidence, RSSI meter + locate view per target.
#include "detectors.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include "shared.h"

namespace Detectors {

// ── signatures ──────────────────────────────────────────────────────
struct Sig {
  const char* detName;         // detector title
  const char* category;        // shown for the hit
  const char* bleNames[4];     // BLE local-name substrings (nullptr end)
  uint16_t bleUuid16[4];       // advertised service UUIDs (0 = end)
  uint16_t bleCompany[2];      // manufacturer company IDs (0 = end)
  const char* wifiSsids[3];    // SSID substrings
  const char* wifiOuis[4];     // BSSID OUI prefixes "xx:xx:xx"
};

static const Sig kSigs[] = {
  { "Flipper Zero", "multitool",
    {"Flipper", nullptr}, {}, {}, {}, {} },
  { "Meshtastic", "mesh node",
    {"Meshtastic", "meshtastic", nullptr}, {}, {0x0059}, {}, {} },
  { "MeshCore", "mesh node",
    {"MeshCore", "meshcore", nullptr}, {}, {}, {}, {} },
  { "Tile Tracker", "tracker",
    {}, {0xFEED}, {}, {}, {} },
  { "Samsung SmartTag", "tracker",
    {}, {}, {0x0075}, {}, {} },
  { "RayBan Meta", "smart glasses",
    {"Ray-Ban", "RayBan", "Stories", nullptr}, {}, {}, {}, {} },
  { "iBeacon", "location beacon",
    {}, {}, {0x004C}, {}, {} },   // + adv type 0x02 checked at runtime
  { "Pineapple", "rogue AP",
    {}, {}, {}, {"Pineapple", "pineapple", nullptr}, {} },
  { "Pwnagotchi", "wifi agent",
    {}, {}, {}, {"pwnagotchi", "Pwnagotchi", nullptr}, {} },
  { "WiFi Cameras", "camera",
    {}, {}, {}, {}, {} },          // OUI list from SD (camera-ouis.txt)
  { "LE Gear", "law enforcement",
    {}, {}, {}, {}, {} },          // OUI list from SD (gear-ouis.txt)
  { "Drone RemoteID", "drone",
    {}, {}, {0x0060}, {}, {} },    // asdot interop co-id + WiFi VSE at runtime
  { "Device Scout", "device",
    {nullptr}, {}, {}, {}, {} },   // ranks ALL BLE devices by persistence
};
static constexpr int kSigCount = sizeof(kSigs) / sizeof(kSigs[0]);

// SD-extendable OUI lists
static char s_camOuis[24][10];
static int s_camOuiCount = 0;
static char s_gearOuis[24][10];
static int s_gearOuiCount = 0;

static void loadOuiFile(const char* path, char list[][10], int* n, int max) {
  *n = 0;
  File f = SD.open(path);
  if (!f) return;
  while (f.available() && *n < max) {
    String l = f.readStringUntil('\n');
    l.trim();
    l.toLowerCase();
    if (l.length() == 8) {
      strncpy(list[*n], l.c_str(), 8);
      list[*n][8] = 0;
      (*n)++;
    }
  }
  f.close();
}

// ── hit list ────────────────────────────────────────────────────────
struct Hit {
  char desc[40];      // name or MAC
  char mac[18];
  int8_t rssi;
  uint8_t confidence; // 1 possible 2 likely 3 confirmed
  uint32_t lastMs;
  uint32_t firstMs;
  uint16_t seen;      // for Device Scout persistence
};
static constexpr int kMaxHits = 14;
static Hit s_hits[kMaxHits];
static int s_hitCount = 0;

static void upsert(const char* desc, const char* mac, int8_t rssi, uint8_t conf) {
  for (int i = 0; i < s_hitCount; i++) {
    if (!strcmp(s_hits[i].mac, mac)) {
      s_hits[i].rssi = rssi;
      s_hits[i].lastMs = millis();
      s_hits[i].seen++;
      if (conf > s_hits[i].confidence) s_hits[i].confidence = conf;
      return;
    }
  }
  if (s_hitCount >= kMaxHits) return;
  Hit& h = s_hits[s_hitCount++];
  strncpy(h.desc, desc, 39); h.desc[39] = 0;
  strncpy(h.mac, mac, 17); h.mac[17] = 0;
  h.rssi = rssi;
  h.confidence = conf;
  h.lastMs = h.firstMs = millis();
  h.seen = 1;
}

static void decayHits() {
  const uint32_t now = millis();
  for (int i = 0; i < s_hitCount; ) {
    if (now - s_hits[i].lastMs > 30000) {
      memmove(&s_hits[i], &s_hits[i + 1], sizeof(Hit) * (s_hitCount - i - 1));
      s_hitCount--;
    } else {
      i++;
    }
  }
}

// ── BLE passive scan ────────────────────────────────────────────────
static const Sig* s_active = nullptr;

static bool bleMatch(const Sig& s, NimBLEAdvertisedDevice* d, const char** outName) {
  if (d->haveName()) {
    const std::string n = d->getName();
    for (int i = 0; i < 4 && s.bleNames[i]; i++) {
      if (!s.bleNames[i][0]) continue;
      if (n.find(s.bleNames[i]) != std::string::npos) {
        *outName = s.bleNames[i];
        return true;
      }
    }
  }
  if (d->haveServiceUUID()) {
    for (int i = 0; i < 4 && s.bleUuid16[i]; i++) {
      if (d->getServiceUUID() == NimBLEUUID(s.bleUuid16[i]) ||
          (d->haveServiceData() &&
           d->getServiceDataUUID() == NimBLEUUID(s.bleUuid16[i]))) {
        *outName = "service";
        return true;
      }
    }
  }
  if (d->haveManufacturerData()) {
    const std::string md = d->getManufacturerData();
    if (md.size() >= 2) {
      const uint16_t co = (uint8_t)md[0] | ((uint8_t)md[1] << 8);
      for (int i = 0; i < 2 && s.bleCompany[i]; i++) {
        if (co == s.bleCompany[i]) {
          *outName = "manufacturer";
          return true;
        }
      }
      // iBeacon: Apple company + adv type byte 0x02 len 0x15
      if (co == 0x004C && md.size() >= 4 && md[2] == 0x02 && md[3] == 0x15 &&
          !strcmp(s.detName, "iBeacon")) {
        *outName = "iBeacon";
        return true;
      }
      // ASTM RemoteID interop: company 0x0060 (asdot)
      if (co == 0x0060 && !strcmp(s.detName, "Drone RemoteID")) {
        *outName = "RemoteID";
        return true;
      }
    }
  }
  return false;
}

class DetCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* d) override {
    if (!s_active || !d) return;
    const char* why = nullptr;
    if (bleMatch(*s_active, d, &why)) {
      const std::string nm = d->haveName() ? d->getName() : why;
      upsert(nm.c_str(), d->getAddress().toString().c_str(),
             (int8_t)d->getRSSI(), 2);
    } else if (!strcmp(s_active->detName, "Device Scout")) {
      // scout: every BLE device, ranked by persistence
      const std::string nm = d->haveName() ? d->getName() : "";
      char label[40];
      snprintf(label, sizeof(label), "%s", nm.empty() ? "(unnamed)" : nm.c_str());
      upsert(label, d->getAddress().toString().c_str(), (int8_t)d->getRSSI(), 1);
    }
  }
};
static DetCb s_cb;

// WiFi VSE sniff for DroneID (beacons carrying ASTM RemoteID)
static void IRAM_ATTR wifiCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || !s_active) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* f = pkt->payload;
  if ((f[0] & 0x0F) != 0x00 || ((f[0] & 0xF0) >> 4) != 0x08) return; // beacon
  const int len = pkt->rx_ctrl.sig_len;
  // walk tagged params looking for vendor-specific 0xDD with drone co-ids
  int i = 36;
  while (i + 2 < len) {
    const uint8_t id = f[i], l = f[i + 1];
    if (id == 0xDD && l >= 4) {
      const uint16_t co = f[i + 2] | (f[i + 3] << 8);
      if (co == 0x0060 || co == 0x0016 || co == 0x0017) {
        char mac[18];
        const uint8_t* b = f + 16;
        snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                 b[0], b[1], b[2], b[3], b[4], b[5]);
        upsert("DroneID beacon", mac, (int8_t)pkt->rx_ctrl.rssi, 3);
      }
    }
    i += 2 + l;
    if (l == 0) break;
  }
}

static bool ouiInList(const char* mac, const char list[][10], int n) {
  char buf[9];
  memcpy(buf, mac, 8);
  buf[8] = 0;
  for (int i = 0; i < n; i++) {
    if (!strcmp(buf, list[i])) return true;
  }
  return false;
}

static void wifiScan(const Sig& s) {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.scanDelete();
  const int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; i++) {
    const String ssid = WiFi.SSID(i);
    const uint8_t* bssid = WiFi.BSSID(i);
    if (!bssid) continue;
    char mac[18];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    bool bySsid = false, byOui = false;
    for (int k = 0; k < 3 && s.wifiSsids[k]; k++) {
      if (ssid.indexOf(s.wifiSsids[k]) >= 0) bySsid = true;
    }
    if (!strcmp(s.detName, "WiFi Cameras")) {
      byOui = ouiInList(mac, s_camOuis, s_camOuiCount);
    } else if (!strcmp(s.detName, "LE Gear")) {
      byOui = ouiInList(mac, s_gearOuis, s_gearOuiCount);
    } else {
      for (int k = 0; k < 4 && s.wifiOuis[k]; k++) {
        if (!strncmp(mac, s.wifiOuis[k], 8)) byOui = true;
      }
    }
    if (bySsid || byOui) {
      upsert(ssid.c_str(), mac, WiFi.RSSI(i), bySsid ? 3 : 2);
    }
  }
  WiFi.scanDelete();
}

// ── per-detector screen ─────────────────────────────────────────────
static void chrome(const char* title) {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("Prev", "Rescan", "Home", "Locate", "Next");
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(String("// ") + title, 6, 28, 2);
  tft.drawFastHLine(0, 38, tft.width(), UI_LINE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("passive receive-only", 8, 44, 1);
  setupTouchscreen();
  redrawTouchButtonBar();
}

static const char* confName(uint8_t c) {
  return c >= 3 ? "CONFIRMED" : (c == 2 ? "LIKELY" : "POSSIBLE");
}
static uint16_t confColor(uint8_t c) {
  return c >= 3 ? 0x07E0 : (c == 2 ? UI_WARN : UI_DIM_TEXT);
}

static void paintHits(int cursor, bool locateMode) {
  const int16_t bottom = touchNavContentBottomY() > 0 ? touchNavContentBottomY()
                                                      : tft.height() - 44;
  tft.fillRect(0, 56, tft.width(), bottom - 56, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  if (s_hitCount == 0) {
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("listening...", 8, 120, 2);
    return;
  }
  int y = 56;
  for (int i = 0; i < s_hitCount && y + 24 < bottom - 40; i++) {
    const bool sel = (i == cursor);
    tft.fillRect(4, y, tft.width() - 8, 22, sel ? UI_LABLE : TFT_BLACK);
    tft.drawRect(4, y, tft.width() - 8, 22, sel ? UI_ICON : UI_LINE);
    tft.setTextColor(sel ? UI_ICON : UI_TEXT, TFT_BLACK);
    char l[44];
    snprintf(l, sizeof(l), "%.28s %d", s_hits[i].desc, s_hits[i].rssi);
    tft.drawString(l, 8, y + 3, 2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(confColor(s_hits[i].confidence), TFT_BLACK);
    if (locateMode && sel) {
      tft.drawString("LOCATE", tft.width() - 8, y + 6, 1);
    } else {
      tft.drawString(confName(s_hits[i].confidence), tft.width() - 8, y + 6, 1);
    }
    tft.setTextDatum(TL_DATUM);
    y += 24;
  }
  // locate meter for the selected target
  if (locateMode && cursor < s_hitCount) {
    const int r = s_hits[cursor].rssi;
    const int v = constrain((r + 100) * 100 / 60, 0, 100);
    tft.drawRect(8, bottom - 34, tft.width() - 16, 12, UI_LINE);
    tft.fillRect(10, bottom - 32, (tft.width() - 20) * v / 100, 8, 0x07E0);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(String(r) + " dBm - walk to peak", 8, bottom - 20, 1);
  }
}

static void runDetector(int sigIdx) {
  const Sig& s = kSigs[sigIdx];
  s_active = &s;
  s_hitCount = 0;
  chrome(s.detName);

  bool wifiEver = s.wifiSsids[0] || s.wifiOuis[0] ||
                  !strcmp(s.detName, "WiFi Cameras") || !strcmp(s.detName, "LE Gear");
  bool wifiDrone = !strcmp(s.detName, "Drone RemoteID");

  pauseBackgroundRadioTasks();

  // BLE observer window (unless detector is WiFi-only)
  const bool useBle = !wifiEver || wifiDrone || true;  // most detectors listen both
  if (useBle) {
    ensureBleStackReady();
    BLEScan* scan = BLEDevice::getScan();
    scan->stop();
    scan->setAdvertisedDeviceCallbacks(&s_cb, true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(100);
    scan->setDuplicateFilter(true);
  }

  int cursor = 0;
  bool locate = false;
  uint32_t lastScan = 0;
  uint32_t lastPaint = 0;

  if (wifiDrone) {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifiCb);
    esp_wifi_set_promiscuous(true);
  }

  for (;;) {
    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
      break;
    }
    const uint32_t now = millis();

    if (isTouchNavButtonPressedEdge(BTN_UP) || isButtonPressed(BTN_UP)) {
      delay(150);
      locate = !locate;
    } else if (isTouchNavButtonPressedEdge(BTN_DOWN) || isButtonPressed(BTN_DOWN)) {
      delay(150);
      if (s_hitCount) cursor = (cursor + 1) % s_hitCount;
    } else if (isTouchNavButtonPressedEdge(BTN_LEFT) || isButtonPressed(BTN_LEFT)) {
      break;   // back to detector list
    } else if (isTouchNavButtonPressedEdge(BTN_RIGHT) || isButtonPressed(BTN_RIGHT)) {
      break;
    }

    // periodic scan cycle
    if (now - lastScan > 6000) {
      lastScan = now;
      if (useBle) BLEDevice::getScan()->start(0, nullptr, false);
      if (wifiEver) wifiScan(s);
      decayHits();
      if (cursor >= s_hitCount) cursor = 0;
    }

    if (now - lastPaint > 400) {
      lastPaint = now;
      paintHits(cursor, locate);
    }
    delay(20);
  }

  if (useBle) BLEDevice::getScan()->stop();
  if (wifiDrone) esp_wifi_set_promiscuous(false);
  s_active = nullptr;
}

// ── BLE Inspector: raw advertisement decode ─────────────────────────
static uint32_t lastPaint = 0;

static void runInspector() {
  chrome("BLE INSPECTOR");
  s_active = nullptr;
  ensureBleStackReady();
  BLEScan* scan = BLEDevice::getScan();
  scan->stop();
  scan->setAdvertisedDeviceCallbacks(&s_cb, true);
  scan->setActiveScan(true);          // request scan response for detail
  scan->setInterval(100);
  scan->setWindow(100);
  scan->setDuplicateFilter(true);
  scan->start(0, nullptr, false);

  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("raw advertisements", 8, 44, 1);

  for (;;) {
    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) break;
    if (millis() - lastPaint > 500) {
      lastPaint = millis();
      paintHits(0, false);
    }
    delay(20);
  }
  scan->stop();
}

// ── launcher ────────────────────────────────────────────────────────
void runLauncher() {
  loadOuiFile("/flock/camera-ouis.txt", s_camOuis, &s_camOuiCount, 24);
  loadOuiFile("/flock/gear-ouis.txt", s_gearOuis, &s_gearOuiCount, 24);

  const int listCount = kSigCount + 1;   // + BLE Inspector
  int cursor = 0;
  bool redraw = true;
  feature_active = true;
  feature_exit_requested = false;

  for (;;) {
    if (redraw) {
      redraw = false;
      setTouchButtonInputEnabled(true);
      setTouchNavLabels(nullptr, nullptr, "Home", nullptr, nullptr);
      featureClearContent(TFT_BLACK);
      drawStatusBar(readBatteryVoltage(), true);
      tft.setTextDatum(ML_DATUM);
      tft.setTextColor(UI_ICON, TFT_BLACK);
      tft.drawString("// DETECTORS", 6, 28, 2);
      tft.drawFastHLine(0, 38, tft.width(), UI_LINE);
      tft.setTextDatum(TL_DATUM);
      for (int i = 0; i < listCount; i++) {
        const int y = 46 + i * 22;
        const bool sel = (i == cursor);
        tft.fillRect(4, y, tft.width() - 8, 20, sel ? UI_LABLE : TFT_BLACK);
        tft.drawRect(4, y, tft.width() - 8, 20, sel ? UI_ICON : UI_LINE);
        tft.setTextColor(sel ? UI_ICON : UI_TEXT, TFT_BLACK);
        tft.drawString(String(sel ? "> " : "  ") +
                       (i < kSigCount ? kSigs[i].detName : "BLE Inspector"),
                       8, y + 4, 2);
      }
      tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
      tft.drawString("passive / receive-only - tap or SELECT", 8,
                     46 + listCount * 22 + 4, 1);
      setupTouchscreen();
      redrawTouchButtonBar();
    }

    if (featureExitButtonPressed()) {
      int hx, hy;
      while (readTouchXYDismiss(hx, hy)) delay(10);
      delay(350);
      break;
    }
    if (isButtonPressed(BTN_UP) || isTouchNavButtonPressedEdge(BTN_UP)) {
      delay(150);
      cursor = (cursor + listCount - 1) % listCount;
      redraw = true;
      continue;
    }
    if (isButtonPressed(BTN_DOWN) || isTouchNavButtonPressedEdge(BTN_DOWN)) {
      delay(150);
      cursor = (cursor + 1) % listCount;
      redraw = true;
      continue;
    }
    if (isButtonPressed(BTN_SELECT)) {
      delay(200);
      if (cursor < kSigCount) runDetector(cursor);
      else runInspector();
      feature_active = true;
      feature_exit_requested = false;
      redraw = true;
      continue;
    }
    int tx, ty;
    if (readTouchXY(tx, ty)) {
      delay(30);
      for (int i = 0; i < listCount; i++) {
        const int y = 46 + i * 22;
        if (ty >= y && ty <= y + 20) {
          if (i < kSigCount) runDetector(i);
          else runInspector();
          feature_active = true;
          feature_exit_requested = false;
          redraw = true;
          break;
        }
      }
      while (readTouchXYDismiss(tx, ty)) delay(10);
      delay(150);
    }
    delay(15);
  }
  setTouchButtonInputEnabled(false);
  feature_active = false;
  feature_exit_requested = false;
  is_main_menu = false;
  showNotification("Detectors", "session ended");
  delay(200);
}

}  // namespace Detectors
