// flock_menu.cpp — Flock Scanner app shell. Tabs: DETECT | SENSORS | SCORE |
// GPS | LOG | SETT. Non-blocking: one phase of the scan cycle runs per pass.
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
enum Tab : uint8_t { T_DETECT, T_SENSORS, T_SCORE, T_GPS, T_LOG, T_SETT, T_COUNT };

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

static void chrome() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("Tab-", nullptr, "Exit", "Pick", "Tab+");
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setupTouchscreen();
  redrawTouchButtonBar();
}

static void tabHeader() {
  static const char* const names[T_COUNT] =
    { "DETECT", "SENSORS", "SCORE", "GPS", "LOG", "SETT" };
  tft.fillRect(0, 22, tft.width(), 20, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("// FLOCK", 6, 24, 2);
  int x = 100;
  for (int i = 0; i < T_COUNT; i++) {
    tft.setTextColor(i == (int)s_tab ? 0x07E0 : UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(names[i], x, 27, 1);
    x += 6 * tft.textWidth(names[i], 1) / 4 + 10;
  }
  tft.drawFastHLine(0, 42, tft.width(), UI_LINE);
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
  const char* label = flock_score::label(score);
  if (!strcmp(label, "QUIET") || !strcmp(label, "POSSIBLE")) return;
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
    showNotification(label, msg);
  }
  beep(score >= 60 ? 3 : 2);
}

// ── phase engine: one radio at a time, never blocking ──
static void runPhase() {
  const uint32_t now = millis();
  switch (s_phase) {
    case P_WIFI:
      if (now - s_phaseStart < 100) {
        flock_wifi::startScan();
      } else {
        int n;
        if (flock_wifi::pollScan(&n) && now - s_phaseStart > flk::kWifiWindowMs) {
          s_phase = P_PROBE; s_phaseStart = now;
        }
      }
      break;
    case P_PROBE:
      if (now - s_phaseStart < 100) flock_wifi::startProbeWindow();
      else if (now - s_phaseStart > flk::kProbeWindowMs) {
        flock_wifi::stopProbeWindow();
        if (s_mode == 1) { s_phase = P_WIFI; }        // wifi-only loops
        else {
          s_phase = s_settings[1] ? P_BLE : P_SENSORS;
          if (s_phase == P_BLE) flock_ble::startWindow();
        }
        s_phaseStart = now;
      }
      break;
    case P_BLE:
      flock_ble::poll();
      if (now - s_phaseStart > flk::kBleWindowMs) {
        flock_ble::stopWindow();
        s_phase = P_SENSORS; s_phaseStart = now;
      }
      break;
    case P_SENSORS:
      if (now - s_lastSensMs > flk::kSensorTickMs) {
        s_lastSensMs = now;
        s_nrfAct = flock_sensors::sampleNrf();
        s_lteStr = flock_sensors::sampleLte();
      }
      if (now - s_phaseStart > 2000) {
        // score + alerts for every wifi hit
        const flock_wifi::Hit* hits = flock_wifi::hits();
        for (int i = 0; i < flock_wifi::hitCount(); i++) {
          const int score = flock_score::compute(
              hits[i].bySsid || hits[i].byOui || hits[i].byProbe,
              flock_ble::matches() > 0, s_lteStr, s_nrfAct, false);
          maybeAlert(hits[i], score);
        }
        s_lastScore = flock_score::compute(
            flock_wifi::hitCount() > 0, flock_ble::matches() > 0,
            s_lteStr, s_nrfAct, false);
        s_phase = P_WIFI; s_phaseStart = now;
      }
      break;
    default: s_phase = P_WIFI; s_phaseStart = now;
  }
}

// ── tab rendering ──
static void drawDetect() {
  const flock_wifi::Hit* hits = flock_wifi::hits();
  const int n = flock_wifi::hitCount();
  tft.fillRect(0, 46, tft.width(), 180, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  if (n == 0) {
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("scanning... no matches yet", 8, 120, 2);
  }
  for (int i = 0; i < n && i < 5; i++) {
    const int y = 50 + i * 26;
    const bool sel = (i == s_cursor);
    tft.fillRect(4, y, tft.width() - 8, 24, sel ? UI_LABLE : TFT_BLACK);
    tft.drawRect(4, y, tft.width() - 8, 24, sel ? UI_ICON : UI_LINE);
    tft.setTextColor(sel ? UI_ICON : UI_TEXT, TFT_BLACK);
    char l[40];
    snprintf(l, sizeof(l), "%s %d dBm", hits[i].mac, hits[i].rssi);
    tft.drawString(l, 10, y + 4, 2);
    tft.setTextColor(0x07E0, TFT_BLACK);
    if (hits[i].bySsid) tft.drawString(hits[i].ssid, 10, y + 16, 1);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(strcmp(confidenceOf(hits[i]), "CONFIRMED") ? UI_WARN : 0x07E0,
                     TFT_BLACK);
    tft.drawString(confidenceOf(hits[i]), tft.width() - 8, y + 8, 1);
    tft.setTextDatum(TL_DATUM);
  }
  // signal meter for the selected target
  if (n > 0 && s_cursor < n) {
    const int r = hits[s_cursor].rssi;
    const int v = constrain((r + 100) * 100 / 60, 0, 100);
    tft.drawRect(8, 236, tft.width() - 16, 10, UI_LINE);
    tft.fillRect(10, 238, (tft.width() - 20) * v / 100, 6, 0x07E0);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(String(r) + " dBm", 8, 248, 1);
  }
}

static void drawSensors() {
  tft.fillRect(0, 46, tft.width(), 190, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("2.4 GHz activity (nRF24 RX)", 8, 50, 1);
  tft.drawRect(8, 64, tft.width() - 16, 10, UI_LINE);
  tft.fillRect(10, 66, (tft.width() - 20) * s_nrfAct / 100, 6, 0x07E0);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.drawString(String(s_nrfAct) + " / 100", 8, 78, 1);

  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("LTE-band burst (CC1101 RSSI)", 8, 104, 1);
  tft.drawRect(8, 118, tft.width() - 16, 10, UI_LINE);
  tft.fillRect(10, 120, (tft.width() - 20) * s_lteStr / 100, 6,
               flock_sensors::lteBurstSeen() ? 0xF800 : 0x07E0);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.drawString(flock_sensors::lteBurstSeen() ? "burst seen" : "quiet", 8, 132, 1);

  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("IR illuminator", 8, 158, 1);
  int adc;
  if (flock_sensors::sampleIr(&adc)) {
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.drawString("adc " + String(adc), 8, 172, 1);
  } else {
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("Needs photodiode (TSOP only)", 8, 172, 1);
  }
}

static void drawScore() {
  tft.fillRect(0, 46, tft.width(), 190, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(0x07E0, TFT_BLACK);
  tft.drawString(String(s_lastScore), tft.width() / 2, 110, 6);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.drawString(flock_score::label(s_lastScore), tft.width() / 2, 170, 3);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  char w[80];
  snprintf(w, sizeof(w), "wifi %d ble %d lte %d 2g %d ir %d",
           flk::kScoreWifi, flk::kScoreBle, flk::kScoreLte,
           flk::kScoreNrf, flk::kScoreIr);
  tft.drawString(w, tft.width() / 2, 205, 1);
  tft.setTextDatum(TL_DATUM);
}

static void drawGps() {
  tft.fillRect(0, 46, tft.width(), 190, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  if (flock_gps::fixValid()) {
    char l[48];
    tft.setTextColor(0x07E0, TFT_BLACK);
    snprintf(l, sizeof(l), "%.5f, %.5f",
             (double)flock_gps::lat(), (double)flock_gps::lon());
    tft.drawString(l, 8, 70, 2);
    char t[10];
    flock_gps::utc(t, sizeof(t));
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.drawString(String("time ") + t, 8, 100, 2);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("geotagging detections", 8, 130, 1);
    tft.drawString("follow alert: 3+ fixes >500 m", 8, 146, 1);
  } else {
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.drawString("no GPS fix", 8, 90, 3);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(s_settings[2] ? "waiting for satellites..."
                                 : "GPS off in SETT", 8, 120, 1);
  }
}

static void drawLog() {
  tft.fillRect(0, 46, tft.width(), 190, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  if (!flock_log::ok()) {
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.drawString(flock_log::lastError(), 8, 90, 2);
    return;
  }
  const int n = flock_log::historyCount();
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("session detections: " + String(n), 8, 50, 1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("csv/json/kml on SD /flock", 8, 64, 1);
  for (int i = 0; i < n && i < 7; i++) {
    const flock_log::Record* r = flock_log::history(i);
    char l[52];
    snprintf(l, sizeof(l), "%s %s %dd %s", r->utc, r->mac, r->rssi,
             r->confidence);
    tft.setTextColor(i == 0 ? UI_TEXT : UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(l, 8, 82 + i * 14, 1);
  }
}

static void drawSett() {
  static const char* const names[5] = {
    "channel hop probes", "BLE scan", "GPS", "sound", "wardrive autolog" };
  tft.fillRect(0, 46, tft.width(), 190, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < 5; i++) {
    const int y = 56 + i * 24;
    const bool sel = (i == s_cursor);
    tft.setTextColor(sel ? UI_ICON : UI_TEXT, TFT_BLACK);
    tft.drawString(String(sel ? "> " : "  ") + names[i], 8, y, 2);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(s_settings[i] ? 0x07E0 : UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(s_settings[i] ? "ON" : "OFF", tft.width() - 10, y, 2);
    tft.setTextDatum(TL_DATUM);
  }
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("signatures: /flock/ouis.txt on SD", 8, 190, 1);
  tft.drawString("saved to NVS (Preferences)", 8, 204, 1);
}

static void paint() {
  tabHeader();
  switch (s_tab) {
    case T_DETECT: drawDetect(); break;
    case T_SENSORS: drawSensors(); break;
    case T_SCORE: drawScore(); break;
    case T_GPS: drawGps(); break;
    case T_LOG: drawLog(); break;
    case T_SETT: drawSett(); break;
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
  s_tab = (Tab)(tab < T_COUNT ? tab : 0);
  s_mode = mode;
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

void appLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    flock_wifi::stopProbeWindow();
    flock_ble::stopWindow();
    return;
  }

  // tabs + list navigation
  if (isTouchNavButtonPressedEdge(BTN_LEFT) || isButtonPressed(BTN_LEFT)) {
    s_tab = (Tab)((s_tab + T_COUNT - 1) % T_COUNT);
    s_cursor = 0;
    paint();
  } else if (isTouchNavButtonPressedEdge(BTN_RIGHT) || isButtonPressed(BTN_RIGHT)) {
    s_tab = (Tab)((s_tab + 1) % T_COUNT);
    s_cursor = 0;
    paint();
  } else if (isTouchNavButtonPressedEdge(BTN_DOWN) || isButtonPressed(BTN_DOWN)) {
    delay(120);
    if (s_tab == T_DETECT) {
      if (s_cursor < flock_wifi::hitCount() - 1) s_cursor++;
    } else if (s_tab == T_SETT) {
      s_cursor = (s_cursor + 1) % 5;
    } else if (s_tab == T_LOG) {
      flock_log::exportDeFlock();
      showNotification("EXPORT", "deflock.csv written to SD");
    }
    paint();
  } else if (isTouchNavButtonPressedEdge(BTN_UP) || isButtonPressed(BTN_UP)) {
    delay(120);
    if (s_tab == T_DETECT) {
      if (s_cursor > 0) s_cursor--;
    } else if (s_tab == T_SETT) {
      s_settings[s_cursor] = !s_settings[s_cursor];
      saveSettings();
    }
    paint();
  }

  // engine
  if (s_settings[2]) flock_gps::poll();
  runPhase();
  static uint32_t s_hb = 0;
  if (millis() - s_hb > 5000) {
    s_hb = millis();
    Serial.printf("[flock] tick phase=%d score=%d wifiHits=%d ble=%d nrf=%d lte=%d\n",
                  (int)s_phase, s_lastScore, flock_wifi::hitCount(),
                  flock_ble::matches(), s_nrfAct, s_lteStr);
  }

  // refresh
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
