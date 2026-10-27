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
#include "flock_link.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"
#include <Preferences.h>
#include <SD.h>
#include <FS.h>

namespace flock_menu {

enum Phase : uint8_t { P_WIFI, P_PROBE, P_BLE, P_SENSORS, P_COUNT };
// Views: tab-free UI — one dashboard plus focused screens the launcher
// can open directly (meter / settings / log).
enum Screen : uint8_t { S_DASH, S_METER, S_SETT, S_LOG };
static Screen s_screen = S_DASH;
static flock_wifi::Hit s_meterTarget;   // selection snapshot for locate
static bool s_meterHas = false;
// legacy tab ids kept only so launcher entries can address screens
enum Tab : uint8_t { T_DETECT, T_SENSORS, T_SCORE, T_GPS, T_LOG, T_SETT };

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

// Tabs removed (issue feedback): everything lives on one dashboard.
static void tabHeader() {}

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
  // iOS link sees every scored match regardless of local alert rules
  if (s_settings[5] && flock_link::active()) {
    flock_link::pushDetection("wifi", h.mac, h.rssi, confidenceOf(h));
  }
  if (!strcmp(lab, "QUIET") || !strcmp(lab, "POSSIBLE")) return;
  if (!flock_log::canAlert(h.mac, h.lastMs)) return;

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
  const bool have = s_meterHas ? true : (n > 0);
  const flock_wifi::Hit* h = s_meterHas ? &s_meterTarget
                           : (n > 0 ? &s_wifi[0] : nullptr);

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
  static const char* const names[6] = {
    "channel-hop probes", "BLE scan", "GPS", "sound", "wardrive autolog",
    "iOS app link (BLE)" };
  tft.fillRect(0, kContentTop, tft.width(), contentBottom() - kContentTop, TFT_BLACK);
  for (int i = 0; i < 6; i++) {
    const int y = 46 + i * 28;
    const bool sel = (i == s_cursor);
    panel(8, y, tft.width() - 16, 26, sel);
    label(names[i], 16, y + 17, sel ? TFT_BLACK : UI_TEXT);
    // Fixed-position toggle pill so ON/OFF never drifts from its label (#1.4)
    const bool on = s_settings[i];
    const int16_t px = tft.width() - 66;
    const int16_t py = y + 5;
    tft.fillRoundRect(px, py, 50, 16, 4, on ? 0x03E0 : UI_LINE);
    tft.drawRoundRect(px, py, 50, 16, 4, on ? 0x07E0 : UI_LABLE);
    tft.fillRect(px + (on ? 28 : 4), py + 2, 18, 12, on ? 0x07E0 : UI_LABLE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(on ? 0x0010 : UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(on ? "ON" : "OFF", px + 14, py + 8, 1);
    tft.setTextDatum(TL_DATUM);
  }
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("UP toggles - extra OUIs: /flock/ouis.txt", 8, 212, 1);
}

// ── the one dashboard: verdict banner, live proof-of-life line, list ──
static void drawDashboard() {
  const int16_t top = 21;
  tft.fillRect(0, top, tft.width(), contentBottom() - top, TFT_BLACK);
  tft.setTextFont(1);

  // verdict banner
  const char* lab = flock_score::label(s_lastScore);
  const bool hot = s_wifiN > 0;
  const uint16_t vc = !strcmp(lab, "CONFIRMED") ? 0x07E0
                    : !strcmp(lab, "LIKELY") ? UI_WARN : UI_DIM_TEXT;
  tft.fillRoundRect(6, top + 4, tft.width() - 12, 26, 4,
                    hot ? vc : UI_LINE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(hot ? TFT_BLACK : UI_TEXT, hot ? vc : TFT_BLACK);
  tft.drawString(hot ? lab : "NO FLOCK DEVICE SEEN", 14, top + 17, 2);
  tft.setTextDatum(MR_DATUM);
  tft.setTextColor(hot ? TFT_BLACK : UI_DIM_TEXT, hot ? vc : TFT_BLACK);
  tft.drawString(String(s_lastScore) + "/100", tft.width() - 14, top + 17, 2);
  tft.setTextDatum(TL_DATUM);

  // proof-of-life: always shows what the radios are doing right now
  static const char* const phaseName[] = { "wifi", "probe", "ble", "sensors" };
  char alive[72];
  snprintf(alive, sizeof(alive), "%s | w:%d b:%d 2g:%d%% lte:%s gps:%s",
           phaseName[s_phase < 4 ? s_phase : 0], s_wifiN, s_bleN, s_nrfAct,
           flock_sensors::lteBurstSeen() ? "burst" : "quiet",
           flock_gps::fixValid() ? "fix" : "--");
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString(alive, 8, top + 36, 1);

  // merged detections: wifi rows then ble rows
  const int rowH = 26, listTop = top + 50;
  const int vis = (contentBottom() - 16 - listTop) / rowH;
  int y = listTop;
  for (int i = 0; i < s_wifiN && i < vis; i++, y += rowH) {
    const bool sel = (i == s_cursor);
    panel(6, y, tft.width() - 12, rowH - 3, sel);
    label(s_wifi[i].ssid[0] ? s_wifi[i].ssid : s_wifi[i].mac, 14, y + 12,
          sel ? TFT_BLACK : UI_TEXT);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(sel ? TFT_BLACK : confColor(confidenceOf(s_wifi[i])), UI_FG);
    tft.drawString(confidenceOf(s_wifi[i]), tft.width() - 14, y + 6, 1);
    tft.setTextColor(sel ? TFT_BLACK : UI_DIM_TEXT, UI_FG);
    tft.drawString(String(s_wifi[i].rssi) + "dBm", tft.width() - 14, y + 17, 1);
    tft.setTextDatum(TL_DATUM);
  }
  for (int k = 0; k < s_bleN && (s_wifiN + k) < vis; k++, y += rowH) {
    const int idx = s_wifiN + k;
    const bool sel = (idx == s_cursor);
    panel(6, y, tft.width() - 12, rowH - 3, sel);
    char bl[44];
    snprintf(bl, sizeof(bl), "[ble] %s",
             s_ble[k].name[0] ? s_ble[k].name : s_ble[k].mac);
    label(bl, 14, y + 12, sel ? TFT_BLACK : UI_TEXT);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(sel ? TFT_BLACK : UI_DIM_TEXT, UI_FG);
    tft.drawString(String(s_ble[k].rssi) + "dBm", tft.width() - 14, y + 12, 1);
    tft.setTextDatum(TL_DATUM);
  }
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("UP/DOWN pick - RIGHT locate - LEFT rescan", 8,
                 contentBottom() - 14, 1);
}

static void refreshSnapshots() {
  s_wifiN = flock_wifi::snapshot(s_wifi, flock_wifi::kMaxHits);
  s_bleN = flock_ble::snapshot(s_ble, flock_ble::kMaxHits);
  const int listMax = (s_screen == S_SETT) ? 5 : (s_wifiN + s_bleN);
  if (listMax <= 0) s_cursor = 0;
  else if (s_cursor >= listMax) s_cursor = listMax - 1;
}

static void paint() {
  refreshSnapshots();
  switch (s_screen) {
    case S_DASH:   drawDashboard(); break;
    case S_METER:  drawMeter(); break;
    case S_SETT:   drawSett(); break;
    case S_LOG:    drawLog(); break;
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
  s_cursor = 0;
  s_meterHas = false;
  if (mode == 3)              { s_screen = S_METER; s_mode = 0; }
  else if (tab == T_SETT)     { s_screen = S_SETT;  s_mode = 0; }
  else if (tab == T_LOG)      { s_screen = S_LOG;   s_mode = 0; }
  else                        { s_screen = S_DASH;  s_mode = mode; }
}

void appSetup() {
  Serial.println("[flock] setup: chrome");
  if (s_settings[5]) flock_link::start();
  // Let the launching tap's latch expire first, otherwise appLoop's exit
  // check reads it as an instant close (looked like an open glitch). #1.1.
  int dx, dy;
  while (readTouchXYDismiss(dx, dy)) delay(10);
  delay(350);
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
  if (s_screen == S_SETT) return 6;
  return s_wifiN + s_bleN;   // dashboard merges wifi + ble rows
}
void appLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    flock_wifi::stopProbeWindow();
    flock_ble::stopWindow();
    return;
  }

  // No tabs — per-screen controls, edge-triggered with debounce.
  static uint32_t s_navDb = 0;
  const uint32_t nowMs = millis();
  if (nowMs - s_navDb > 280) {
    if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
      s_navDb = nowMs;
      if (s_screen == S_SETT) {
        s_cursor = (s_cursor + 1) % 6;
      } else if (s_screen == S_LOG) {
        flock_log::exportDeFlock();
        showNotification("EXPORT", "deflock.csv written to SD");
      } else if (s_screen == S_DASH && listLen() > 0 && s_cursor < listLen() - 1) {
        s_cursor++;
      } else if (s_screen == S_METER) {
        s_screen = S_DASH;
      }
      paint();
    } else if (isTouchNavButtonPressedEdge(BTN_UP)) {
      s_navDb = nowMs;
      if (s_screen == S_SETT) {
        s_settings[s_cursor] = !s_settings[s_cursor];
        saveSettings();
        if (s_cursor == 5) {   // iOS link toggle
          if (s_settings[5]) flock_link::start();
          else               flock_link::stop();
        }
      } else if (s_screen == S_DASH && listLen() > 0 && s_cursor > 0) {
        s_cursor--;
      } else if (s_screen == S_METER) {
        s_screen = S_DASH;
      }
      paint();
    } else if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
      s_navDb = nowMs;
      if (s_screen == S_DASH) {
        // locate the picked target: snapshot it, open the meter
        if (s_cursor < s_wifiN) {
          s_meterTarget = s_wifi[s_cursor];
          s_meterHas = true;
          s_screen = S_METER;
        } else {
          s_meterHas = false;
          s_screen = S_METER;
        }
        paint();
      } else if (s_screen == S_METER) {
        s_screen = S_DASH;
        paint();
      }
    } else if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
      s_navDb = nowMs;
      if (s_screen == S_DASH) {
        // rescan now: restart the engine from the WiFi phase
        flock_wifi::stopProbeWindow();
        flock_ble::stopWindow();
        s_phase = P_WIFI;
        s_phaseStart = 0;
        paint();
      } else if (s_screen == S_METER || s_screen == S_LOG) {
        s_screen = S_DASH;
        paint();
      }
    }
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

  // Repaint only when the tab content changed: fixed-cadence full repaints
  // cleared and redrew the whole body twice a second = flicker. Issue #1.2.
  const uint32_t nowP = millis();
  if (nowP - s_lastPaint > 700) {
    static uint32_t s_lastHash = 0;
    const uint32_t h = (uint32_t)s_phase * 1000003u
                     ^ (uint32_t)s_wifiN * 83492791u
                     ^ (uint32_t)s_bleN * 19349663u
                     ^ (uint32_t)s_lastScore * 73856093u
                     ^ (uint32_t)s_nrfAct * 2654435761u
                     ^ (uint32_t)s_lteStr * 40503u
                     ^ (s_cursor + 1) * 6151u;
    if (h != s_lastHash) {
      s_lastHash = h;
      paint();
    }
    s_lastPaint = nowP;
  }
  delay(15);
}

void exit() {
  flock_wifi::stopProbeWindow();
  flock_ble::stopWindow();
  flock_link::stop();
}

}  // namespace flock_menu
