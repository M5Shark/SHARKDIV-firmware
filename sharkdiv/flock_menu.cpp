// flock_menu.cpp — Flock Scanner app. Tabs: DETECT | SENSORS | SCORE | GPS |
// LOG | SETT, plus a focused SIGNAL meter. SHARKDIV brand UI (green on black,
// glass panels). Non-blocking: one phase of the scan cycle runs per pass.
// Everything is receive-only; see flock_config.h for pins and weights.
#include "flock_menu.h"
#include "flock_config.h"
#include "flock_wifi.h"
#include "flock_ble.h"
#include "flock_sensors.h"
#include "flock_gps.h"
#include "flock_log.h"
#include "flock_score.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"
#include <Preferences.h>
#include <SD.h>
#include <FS.h>

namespace flock_menu {

enum Phase : uint8_t { P_WIFI, P_PROBE, P_BLE, P_SENSORS, P_COUNT };
// Views: the six tabs plus a focused signal meter the launcher opens directly.
enum Tab : uint8_t { T_DETECT, T_SENSORS, T_SCORE, T_GPS, T_LOG, T_SETT, T_COUNT,
                     T_METER = 100 };

static Phase s_phase = P_WIFI;
static uint32_t s_phaseStart = 0;
static Tab s_tab = T_DETECT;
static uint8_t s_mode = 0;             // 0 all / 1 wifi / 2 ble
static int s_cursor = 0;               // list cursor / selected target
static int s_lastScore = 0;
static uint32_t s_lastPaint = 0;
static uint32_t s_lastSensMs = 0;
static int s_nrfAct = 0, s_lteStr = 0;
static bool s_settings[6] = { true, true, true, true, true, false };
// chHop, bleOn, gpsOn, sound, wardrive, irDisabledNote
static Preferences s_prefs;

// Local copies refreshed once per paint so a scan-task write can't change the
// list mid-draw.
static flock_wifi::Hit s_wifi[flock_wifi::kMaxHits];
static int s_wifiN = 0;
static flock_ble::Hit s_ble[flock_ble::kMaxHits];
static int s_bleN = 0;

static const int kContentTop = 44;     // below the status bar + tab strip
static int contentBottom() { return touchNavContentBottomY(); }

// ── brand drawing helpers ───────────────────────────────────────────────
static void label(const char* s, int x, int baselineY, uint16_t color) {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(L_BASELINE);
  tft.setTextColor(color);
  tft.drawString(s, x, baselineY);
  tft.setTextFont(1);
  tft.setTextDatum(TL_DATUM);
}
static void panel(int x, int y, int w, int h, bool sel) {
  if (sel) {
    tft.fillRoundRect(x, y, w, h, 8, UI_ICON);
  } else {
    tft.fillRoundRect(x, y, w, h, 8, UI_FG);
    tft.drawRoundRect(x, y, w, h, 8, UI_LINE);
  }
}
// A horizontal meter: track + green fill scaled from an RSSI in dBm.
static void rssiBar(int x, int y, int w, int rssi, uint16_t fill) {
  const int pct = constrain((rssi + 95) * 100 / 60, 0, 100);
  tft.drawRoundRect(x, y, w, 8, 2, UI_LINE);
  if (pct > 0) tft.fillRoundRect(x + 1, y + 1, (w - 2) * pct / 100, 6, 1, fill);
}
static uint16_t confColor(const char* c) {
  if (!strcmp(c, "CONFIRMED")) return UI_ICON;
  if (!strcmp(c, "LIKELY")) return UI_WARN;
  return UI_DIM_TEXT;
}

static void chrome() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("<Tab", nullptr, "Exit", nullptr, "Tab>");
  featureClearContent(TFT_BLACK);
  setStatusTitle("FLOCK");
  drawStatusBar(readBatteryVoltage(), true);
  setupTouchscreen();
  redrawTouchButtonBar();
}

// A compact tab strip just under the status bar; the active tab is a green pill.
static void tabHeader() {
  if (s_tab == T_METER) return;        // the meter is a focused view, no strip
  static const char* const names[T_COUNT] =
    { "DETECT", "2.4G", "SCORE", "GPS", "LOG", "SETT" };
  tft.fillRect(0, 21, tft.width(), 22, TFT_BLACK);
  int x = 4;
  tft.setTextFont(1);
  for (int i = 0; i < T_COUNT; i++) {
    const int w = tft.textWidth(names[i], 1) + 10;
    const bool on = (i == (int)s_tab);
    if (on) tft.fillRoundRect(x, 24, w, 15, 4, UI_ICON);
    else    tft.drawRoundRect(x, 24, w, 15, 4, UI_LINE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? TFT_BLACK : UI_DIM_TEXT, on ? UI_ICON : TFT_BLACK);
    tft.drawString(names[i], x + w / 2, 32, 1);
    x += w + 3;
  }
  tft.setTextDatum(TL_DATUM);
}

static const char* confidenceOf(const flock_wifi::Hit& h) {
  if (h.bySsid) return "CONFIRMED";
  if (h.byOui && h.byProbe) return "CONFIRMED";
  if (h.byOui || h.byProbe) return "LIKELY";
  return "POSSIBLE";
}

static void beep(int n) {
  if (!s_settings[3]) return;
  for (int i = 0; i < n; i++) {
    ledcWriteTone(1, 2000);
    delay(60);
    ledcWriteTone(1, 0);
    delay(60);
  }
}

static void maybeAlert(const flock_wifi::Hit& h, int score) {
  const char* lab = flock_score::label(score);
  if (!strcmp(lab, "QUIET") || !strcmp(lab, "POSSIBLE")) return;
  if (!flock_log::canAlert(h.mac)) return;

  flock_log::Record r;
  strncpy(r.mac, h.mac, 17); r.mac[17] = 0;
  strncpy(r.ssid, h.ssid, 32); r.ssid[32] = 0;
  r.rssi = h.rssi;
  r.score = score;
  r.confidence = confidenceOf(h);
  r.lat = flock_gps::fixValid() ? flock_gps::lat() : NAN;
  r.lon = flock_gps::fixValid() ? flock_gps::lon() : NAN;
  flock_gps::utc(r.utc, sizeof(r.utc));
  r.follow = flock_gps::followAlert(h.mac);
  flock_log::add(r);
  if (r.follow) {
    showNotification("FOLLOW ALERT", "Same device at 3+ locations");
  } else {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s %d dBm (score %d)", h.mac, h.rssi, score);
    showNotification(lab, msg);
  }
  beep(score >= 60 ? 3 : 2);
}

// ── phase engine: one radio at a time, never blocking ──
static void scoreAndAlert() {
  flock_wifi::Hit hits[flock_wifi::kMaxHits];
  const int n = flock_wifi::snapshot(hits, flock_wifi::kMaxHits);
  const bool ble = flock_ble::matches() > 0;
  for (int i = 0; i < n; i++) {
    const int score = flock_score::compute(
        hits[i].bySsid || hits[i].byOui || hits[i].byProbe,
        ble, s_lteStr, s_nrfAct, false);
    maybeAlert(hits[i], score);
  }
  s_lastScore = flock_score::compute(n > 0, ble, s_lteStr, s_nrfAct, false);
}

static void runPhase() {
  const uint32_t now = millis();
  switch (s_phase) {
    case P_WIFI:
      if (now - s_phaseStart < 100) {
        flock_wifi::startScan();
      } else {
        int n;
        if (flock_wifi::pollScan(&n) && now - s_phaseStart > flk::kWifiWindowMs) {
          s_phase = (s_mode == 2) ? P_BLE : P_PROBE;   // BLE-only skips probes
          if (s_phase == P_BLE) flock_ble::startWindow();
          s_phaseStart = now;
        }
      }
      break;
    case P_PROBE:
      if (now - s_phaseStart < 100) flock_wifi::startProbeWindow();
      else if (now - s_phaseStart > flk::kProbeWindowMs) {
        flock_wifi::stopProbeWindow();
        // BLE runs unless this is a Wi-Fi-only tool or BLE is turned off.
        s_phase = (s_mode != 1 && s_settings[1]) ? P_BLE : P_SENSORS;
        if (s_phase == P_BLE) flock_ble::startWindow();
        s_phaseStart = now;
      }
      break;
    case P_BLE:
      flock_ble::poll();
      if (now - s_phaseStart > flk::kBleWindowMs) {
        flock_ble::stopWindow();
        s_phase = (s_mode == 2) ? P_WIFI : P_SENSORS;   // BLE-only loops back
        s_phaseStart = now;
      }
      break;
    case P_SENSORS:
      if (now - s_lastSensMs > flk::kSensorTickMs) {
        s_lastSensMs = now;
        s_nrfAct = flock_sensors::sampleNrf();
        s_lteStr = flock_sensors::sampleLte();
      }
      if (now - s_phaseStart > 2000) {
        scoreAndAlert();                 // runs in every mode now
        s_phase = P_WIFI; s_phaseStart = now;
      }
      break;
    default: s_phase = P_WIFI; s_phaseStart = now;
  }
}

// ── tab rendering ──
static void drawDetect() {
  const bool bleView = (s_mode == 2);
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  const int n = bleView ? s_bleN : s_wifiN;
  if (n == 0) {
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(bleView ? "scanning BLE - nothing yet"
                           : "scanning - no matches yet",
                   tft.width() / 2, 130, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  const int rowH = 40, top = kContentTop + 2;
  const int vis = (contentBottom() - top) / rowH;
  for (int i = 0; i < n && i < vis; i++) {
    const int y = top + i * rowH;
    const bool sel = (i == s_cursor);
    panel(6, y, tft.width() - 12, rowH - 4, sel);
    const uint16_t fg = sel ? TFT_BLACK : UI_TEXT;
    if (bleView) {
      label(s_ble[i].name[0] ? s_ble[i].name : s_ble[i].mac, 14, y + 16, fg);
      tft.setTextColor(sel ? TFT_BLACK : UI_DIM_TEXT);
      tft.drawString(s_ble[i].mac, 14, y + 20, 1);
      rssiBar(tft.width() - 96, y + 8, 80, s_ble[i].rssi, sel ? TFT_BLACK : UI_ICON);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(sel ? TFT_BLACK : UI_DIM_TEXT);
      tft.drawString(String(s_ble[i].rssi) + "dBm", tft.width() - 14, y + 22, 1);
      tft.setTextDatum(TL_DATUM);
    } else {
      const char* conf = confidenceOf(s_wifi[i]);
      label(s_wifi[i].ssid[0] ? s_wifi[i].ssid : s_wifi[i].mac, 14, y + 15, fg);
      tft.setTextColor(sel ? TFT_BLACK : UI_DIM_TEXT);
      tft.drawString(s_wifi[i].mac, 14, y + 20, 1);
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(sel ? TFT_BLACK : confColor(conf));
      tft.drawString(conf, tft.width() - 14, y + 7, 1);
      tft.setTextColor(sel ? TFT_BLACK : UI_DIM_TEXT);
      tft.drawString(String(s_wifi[i].rssi) + " dBm", tft.width() - 14, y + 20, 1);
      tft.setTextDatum(TL_DATUM);
      rssiBar(14, y + 24, 120, s_wifi[i].rssi, sel ? TFT_BLACK : UI_ICON);
    }
  }
}

// Big focused meter for the strongest target (Signal Meter launcher entry).
static void drawMeter() {
  tft.fillRect(0, 21, tft.width(), contentBottom() - 21, TFT_BLACK);
  tft.setTextFont(1);
  const int n = s_wifiN;
  const bool have = n > 0;
  const flock_wifi::Hit* h = have ? &s_wifi[0] : nullptr;

  panel(8, 26, tft.width() - 16, 30, false);
  tft.setTextColor(UI_DIM_TEXT, UI_FG);
  tft.drawString("TARGET", 16, 32, 1);
  label(have ? (h->ssid[0] ? h->ssid : h->mac) : "no target yet", 16, 50,
        have ? UI_ICON : UI_DIM_TEXT);

  const int rssi = have ? h->rssi : -100;
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(have ? String(rssi) : "--", tft.width() / 2 - 16, 104);
  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("dBm", tft.width() / 2 + 34, 100);
  tft.setTextDatum(TL_DATUM);

  rssiBar(16, 120, tft.width() - 32, rssi, UI_ICON);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("-95", 16, 132, 1);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("-35", tft.width() - 16, 132, 1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  const char* hint = !have ? "move around to find a camera"
                   : rssi > -50 ? "VERY CLOSE"
                   : rssi > -70 ? "warmer - getting closer"
                                : "weak - keep moving";
  tft.drawString(hint, tft.width() / 2, 160, 2);
  tft.setTextDatum(TL_DATUM);
  if (have) {
    tft.setTextColor(confColor(confidenceOf(*h)), TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(confidenceOf(*h), tft.width() / 2, 184, 2);
    tft.setTextDatum(TL_DATUM);
  }
}

static void drawSensors() {
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  const int w = tft.width() - 16;
  // 2.4 GHz activity
  panel(8, 48, w, 42, false);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("2.4 GHz activity  (nRF24 RX)", 16, 54, 1);
  tft.drawRoundRect(16, 70, w - 16, 10, 2, UI_LINE);
  tft.fillRoundRect(17, 71, (w - 18) * s_nrfAct / 100, 8, 1, UI_ICON);
  // LTE burst
  panel(8, 96, w, 42, false);
  const bool burst = flock_sensors::lteBurstSeen();
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("LTE-band burst  (CC1101 RSSI)", 16, 102, 1);
  tft.drawRoundRect(16, 118, w - 16, 10, 2, UI_LINE);
  tft.fillRoundRect(17, 119, (w - 18) * s_lteStr / 100, 8, 1, burst ? TFT_RED : UI_ICON);
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(burst ? TFT_RED : UI_DIM_TEXT, UI_FG);
  tft.drawString(burst ? "BURST" : "quiet", 8 + w - 6, 102, 1);
  tft.setTextDatum(TL_DATUM);
  // IR
  panel(8, 144, w, 42, false);
  tft.setTextColor(UI_ICON, UI_FG);
  tft.drawString("IR illuminator", 16, 150, 1);
  int adc;
  if (flock_sensors::sampleIr(&adc)) {
    tft.setTextColor(UI_TEXT, UI_FG);
    tft.drawString("adc " + String(adc), 16, 166, 1);
  } else {
    tft.setTextColor(UI_DIM_TEXT, UI_FG);
    tft.drawString("needs photodiode (TSOP only)", 16, 166, 1);
  }
}

static void drawScore() {
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  const char* lab = flock_score::label(s_lastScore);
  const uint16_t c = confColor(!strcmp(lab, "CONFIRMED") ? "CONFIRMED"
                             : !strcmp(lab, "LIKELY") ? "LIKELY" : "x");
  // ring-ish: a thick arc faked with a filled wedge is costly; use a big number
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(c, TFT_BLACK);
  tft.drawString(String(s_lastScore), tft.width() / 2, 96);
  tft.setTextFont(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("/ 100", tft.width() / 2, 120, 1);
  tft.setTextColor(c, TFT_BLACK);
  tft.drawString(lab, tft.width() / 2, 150, 2);
  tft.setTextDatum(TL_DATUM);
  // evidence meter
  const int w = tft.width() - 32;
  tft.drawRoundRect(16, 172, w, 10, 2, UI_LINE);
  tft.fillRoundRect(17, 173, (w - 2) * constrain(s_lastScore, 0, 100) / 100, 8, 1, c);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  char line[72];
  snprintf(line, sizeof(line), "wifi%d ble%d lte%d 2g%d ir%d",
           flk::kScoreWifi, flk::kScoreBle, flk::kScoreLte,
           flk::kScoreNrf, flk::kScoreIr);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(line, tft.width() / 2, 196, 1);
  tft.setTextDatum(TL_DATUM);
}

static void drawGps() {
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  const int w = tft.width() - 16;
  panel(8, 52, w, 150, false);
  if (flock_gps::fixValid()) {
    char l[48];
    tft.setTextColor(UI_DIM_TEXT, UI_FG);
    tft.drawString("FIX", 18, 60, 1);
    snprintf(l, sizeof(l), "%.5f, %.5f",
             (double)flock_gps::lat(), (double)flock_gps::lon());
    label(l, 18, 82, UI_ICON);
    char t[10]; flock_gps::utc(t, sizeof(t));
    tft.setTextColor(UI_TEXT, UI_FG);
    tft.drawString(String("UTC  ") + t, 18, 100, 2);
    tft.setTextColor(UI_DIM_TEXT, UI_FG);
    tft.drawString("geotagging every detection", 18, 130, 1);
    tft.drawString("follow alert: 3+ fixes >500 m", 18, 146, 1);
  } else {
    tft.setTextColor(UI_WARN, UI_FG);
    label("no GPS fix", 18, 90, UI_WARN);
    tft.setTextColor(UI_DIM_TEXT, UI_FG);
    tft.drawString(s_settings[2] ? "waiting for satellites..."
                                 : "GPS is OFF in SETT", 18, 120, 1);
  }
}

static void drawLog() {
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  if (!flock_log::ok()) {
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.drawString(flock_log::lastError(), 8, 90, 2);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("insert an SD card to log + export", 8, 112, 1);
    return;
  }
  const int n = flock_log::historyCount();
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("session detections: " + String(n), 8, 48, 1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("DOWN = export DeFlock csv to SD", 8, 62, 1);
  // newest first
  const int rows = 6, top = 78;
  for (int k = 0; k < n && k < rows; k++) {
    const int idx = n - 1 - k;
    const flock_log::Record* r = flock_log::history(idx);
    if (!r) continue;
    const int y = top + k * 22;
    panel(6, y, tft.width() - 12, 20, false);
    tft.setTextColor(k == 0 ? UI_TEXT : UI_DIM_TEXT, UI_FG);
    char l[48];
    snprintf(l, sizeof(l), "%s  %s", r->utc, r->mac);
    tft.drawString(l, 12, y + 6, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(confColor(r->confidence), UI_FG);
    tft.drawString(String(r->rssi) + "dBm", tft.width() - 12, y + 6, 1);
    tft.setTextDatum(TL_DATUM);
  }
}

static void drawSett() {
  static const char* const names[5] = {
    "channel-hop probes", "BLE scan", "GPS", "sound", "wardrive autolog" };
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  for (int i = 0; i < 5; i++) {
    const int y = 50 + i * 30;
    const bool sel = (i == s_cursor);
    panel(8, y, tft.width() - 16, 26, sel);
    label(names[i], 16, y + 17, sel ? TFT_BLACK : UI_TEXT);
    tft.setTextDatum(TR_DATUM);
    const bool on = s_settings[i];
    tft.setTextColor(sel ? TFT_BLACK : (on ? UI_ICON : UI_DIM_TEXT), sel ? UI_ICON : UI_FG);
    tft.drawString(on ? "ON" : "OFF", tft.width() - 18, y + 17, 2);
    tft.setTextDatum(TL_DATUM);
  }
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("UP toggles - extra OUIs: /flock/ouis.txt", 8, 212, 1);
}

static void refreshSnapshots() {
  s_wifiN = flock_wifi::snapshot(s_wifi, flock_wifi::kMaxHits);
  s_bleN = flock_ble::snapshot(s_ble, flock_ble::kMaxHits);
  const int listMax = (s_tab == T_DETECT) ? (s_mode == 2 ? s_bleN : s_wifiN)
                    : (s_tab == T_SETT) ? 5 : 0;
  if (listMax <= 0) s_cursor = 0;
  else if (s_cursor >= listMax) s_cursor = listMax - 1;
}

static void paint() {
  refreshSnapshots();
  tabHeader();
  switch (s_tab) {
    case T_DETECT: drawDetect(); break;
    case T_SENSORS: drawSensors(); break;
    case T_SCORE: drawScore(); break;
    case T_GPS: drawGps(); break;
    case T_LOG: drawLog(); break;
    case T_SETT: drawSett(); break;
    case T_METER: drawMeter(); break;
    default: break;
  }
}

static void loadSettings() {
  static const char* const kKeys[6] = { "hop", "ble", "gps", "snd", "ward", "x5" };
  s_prefs.begin("flock", true);
  for (int i = 0; i < 6; i++) {
    s_settings[i] = s_prefs.getBool(kKeys[i], s_settings[i]);
  }
  s_prefs.end();
}
static void saveSettings() {
  static const char* const kKeys[6] = { "hop", "ble", "gps", "snd", "ward", "x5" };
  s_prefs.begin("flock", false);
  for (int i = 0; i < 6; i++) {
    s_prefs.putBool(kKeys[i], s_settings[i]);
  }
  s_prefs.end();
}

// extra OUIs from SD (optional)
static void loadOuis() {
  if (!flock_log::ok()) return;
  File f = SD.open("/flock/ouis.txt");
  if (!f) return;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    l.toLowerCase();
    if (l.length() == 8) {
      flock_wifi::addOui(l.c_str());
    }
  }
  f.close();
}

void openAt(uint8_t tab, uint8_t mode) {
  // mode 3 from the launcher means the focused Signal Meter view.
  if (mode == 3) { s_tab = T_METER; s_mode = 0; }
  else           { s_tab = (Tab)(tab < T_COUNT ? tab : 0); s_mode = mode; }
  s_cursor = 0;
}

void appSetup() {
  Serial.println("[flock] setup: chrome");
  pauseBackgroundRadioTasks();
  chrome();
  loadSettings();
  Serial.println("[flock] setup: radios");
  flock_wifi::begin();
  flock_ble::begin();
  flock_sensors::begin();
  flock_gps::begin();
  Serial.println("[flock] setup: sd");
  flock_log::begin();
  loadOuis();
  Serial.println("[flock] setup: done");
  s_phase = (s_mode == 2) ? P_BLE : P_WIFI;
  if (s_phase == P_BLE) flock_ble::startWindow();
  s_phaseStart = millis();
  s_lastSensMs = 0;
  paint();
}

// A tab is active (DETECT/METER) if its list can take UP/DOWN as navigation.
static int listLen() {
  if (s_tab == T_DETECT) return s_mode == 2 ? s_bleN : s_wifiN;
  if (s_tab == T_SETT) return 5;
  return 0;
}
static void cycleTab(int dir) {
  if (s_tab == T_METER) { s_tab = T_DETECT; }     // leave the meter into DETECT
  else s_tab = (Tab)(((int)s_tab + T_COUNT + dir) % T_COUNT);
  s_cursor = 0;
  paint();
}

void appLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    flock_wifi::stopProbeWindow();
    flock_ble::stopWindow();
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT) || isButtonPressed(BTN_LEFT)) {
    cycleTab(-1);
  } else if (isTouchNavButtonPressedEdge(BTN_RIGHT) || isButtonPressed(BTN_RIGHT)) {
    cycleTab(+1);
  } else if (isTouchNavButtonPressedEdge(BTN_DOWN) || isButtonPressed(BTN_DOWN)) {
    delay(120);
    if (s_tab == T_SETT) {
      s_cursor = (s_cursor + 1) % 5;
    } else if (s_tab == T_LOG) {
      flock_log::exportDeFlock();
      showNotification("EXPORT", "deflock.csv written to SD");
    } else if (listLen() > 0 && s_cursor < listLen() - 1) {
      s_cursor++;
    }
    paint();
  } else if (isTouchNavButtonPressedEdge(BTN_UP) || isButtonPressed(BTN_UP)) {
    delay(120);
    if (s_tab == T_SETT) {
      s_settings[s_cursor] = !s_settings[s_cursor];
      saveSettings();
    } else if (listLen() > 0 && s_cursor > 0) {
      s_cursor--;
    }
    paint();
  }

  if (s_settings[2]) flock_gps::poll();
  runPhase();
  static uint32_t s_hb = 0;
  if (millis() - s_hb > 5000) {
    s_hb = millis();
    Serial.printf("[flock] tick phase=%d score=%d wifi=%d ble=%d nrf=%d lte=%d\n",
                  (int)s_phase, s_lastScore, flock_wifi::hitCount(),
                  flock_ble::matches(), s_nrfAct, s_lteStr);
  }

  if (millis() - s_lastPaint > 600) {
    s_lastPaint = millis();
    paint();
  }
  delay(15);
}

void exit() {
  flock_wifi::stopProbeWindow();
  flock_ble::stopWindow();
}

}  // namespace flock_menu
