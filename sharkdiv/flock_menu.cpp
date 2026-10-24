// Flock candidate receiver: bounded radio phases, explicit health and controls.
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
#include <cmath>

namespace flock_menu {
enum Phase : uint8_t { P_WIFI, P_PROBE, P_BLE, P_SENSORS };
enum Tab : uint8_t { T_DETECT, T_SENSORS, T_SCORE, T_GPS, T_LOG, T_SETT, T_COUNT, T_METER = 100 };
static Phase s_phase = P_WIFI;
static Tab s_tab = T_DETECT;
static uint8_t s_mode = 0; // 0 all, 1 Wi-Fi only, 2 BLE only
static bool s_phaseEntered = false, s_phaseOk = false, s_started = false;
static uint32_t s_phaseStart = 0, s_lastPaint = 0;
static int s_cursor = 0, s_lastScore = 0, s_nrfAct = -1, s_lteStr = -1;
static bool s_settings[5] = {true, true, true, true, true};
static Preferences s_prefs;
static flock_wifi::Hit s_wifi[flock_wifi::kMaxHits];
static flock_ble::Hit s_ble[flock_ble::kMaxHits];
static int s_wifiN = 0, s_bleN = 0;
static constexpr int kContentTop = 44, kListTop = 88;
static int contentBottom() { return touchNavContentBottomY(); }
static const char* confidenceOf(const flock_wifi::Hit& h) {
  if (h.byFingerprint) return "PROBE FINGERPRINT";
  if (h.bySsid) return "SSID MATCH";
  if (h.byProbe) return "OUI + PROBE";
  return "VENDOR OUI";
}
static const char* bleEvidence(const flock_ble::Hit& h) {
  if (h.evidence & 2) return "MANUFACTURER DATA";
  if (h.evidence & 4) return "SERVICE UUID";
  return "NAME MATCH";
}
static int totalHits() { return s_wifiN + s_bleN; }
static void text(const String& value, int x, int y, uint16_t color = UI_TEXT, int font = 1, int width = 0) {
  tft.setTextFont(font); tft.setTextSize(1); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color);
  String shown = value;
  const int available = width ? width : tft.width() - x - 8;
  if (tft.textWidth(shown) > available) {
    while (shown.length() && tft.textWidth(shown + "...") > available) shown.remove(shown.length() - 1);
    shown += "...";
  }
  tft.drawString(shown, x, y);
  tft.setTextFont(1);
}
static void nav() {
  const char* down = "Next"; const char* up = "Prev";
  if (s_tab == T_SETT) up = "Toggle";
  else if (s_tab == T_LOG) { down = "Export"; up = "Older"; }
  else if (s_tab != T_DETECT && s_tab != T_METER) down = up = nullptr;
  setTouchNavLabels("<Tab", down, "Exit", up, "Tab>");
  redrawTouchButtonBar();
}
static void panel(int x, int y, int w, int h, bool selected) {
  tft.fillRect(x, y, w, h, selected ? UI_ICON : TFT_BLACK);
  if (!selected) tft.drawFastHLine(x, y+h-1, w, UI_LINE);
}
static void meter(int x, int y, int w, int percent) {
  tft.drawRect(x, y, w, 9, UI_LINE);
  const int fill = (w - 2) * constrain(percent, 0, 100) / 100;
  if (fill) tft.fillRect(x+1, y+1, fill, 7, UI_ICON);
}
static void tabHeader() {
  static const char* names[T_COUNT] = {"MATCH", "RF", "INFO", "GPS", "LOG", "SET"};
  tft.fillRect(0, 20, tft.width(), 23, TFT_BLACK);
  for (int i = 0; i < T_COUNT; ++i) {
    const int x = 4 + i * 39;
    if (i == int(s_tab)) tft.fillRect(x, 24, 36, 15, UI_ICON);
    text(names[i], x+3, 28, i == int(s_tab) ? TFT_BLACK : UI_DIM_TEXT, 1, 33);
  }
}
static void refreshSnapshots() {
  char selectedMac[18] = "";
  const bool selecting = s_tab == T_DETECT || s_tab == T_METER;
  const bool selectedWifi = s_cursor < s_wifiN;
  if (selecting && s_cursor >= 0 && s_cursor < totalHits())
    snprintf(selectedMac, sizeof(selectedMac), "%s", selectedWifi ? s_wifi[s_cursor].mac : s_ble[s_cursor-s_wifiN].mac);
  s_wifiN = s_mode == 2 ? 0 : flock_wifi::snapshot(s_wifi, flock_wifi::kMaxHits);
  s_bleN = s_mode == 1 || !s_settings[1] ? 0 : flock_ble::snapshot(s_ble, flock_ble::kMaxHits);
  if (selectedMac[0]) {
    if (selectedWifi) { for (int i = 0; i < s_wifiN; ++i) if (!strcmp(selectedMac, s_wifi[i].mac)) { s_cursor = i; break; } }
    else { for (int i = 0; i < s_bleN; ++i) if (!strcmp(selectedMac, s_ble[i].mac)) { s_cursor = s_wifiN+i; break; } }
  }
  s_lastScore = flock_score::compute(s_wifiN > 0, s_bleN > 0, 0, 0, false);
  const int count = s_tab == T_SETT ? 5 : s_tab == T_LOG ? flock_log::historyCount() : totalHits();
  if (count <= 0) s_cursor = 0;
  else s_cursor = constrain(s_cursor, 0, count - 1);
}
static void recordMatch(const char* mac, const char* name, int rssi, const char* evidence, int index, uint32_t observedMs) {
  if (!flock_log::canAlert(mac, observedMs)) return;
  flock_log::Record r = {};
  snprintf(r.mac, sizeof(r.mac), "%s", mac); snprintf(r.ssid, sizeof(r.ssid), "%s", name);
  r.rssi = rssi; r.score = index; r.confidence = evidence;
  r.lat = flock_gps::lat(); r.lon = flock_gps::lon();
  flock_gps::utc(r.utc, sizeof(r.utc)); r.follow = flock_gps::followAlert(mac);
  flock_log::add(r, s_settings[4]);
  if (s_settings[3]) { ledcWriteTone(1, 2000); delay(50); ledcWriteTone(1, 0); }
}
static void scoreAndAlert() {
  refreshSnapshots();
  for (int i = 0; i < s_wifiN; ++i) recordMatch(s_wifi[i].mac, s_wifi[i].ssid, s_wifi[i].rssi, confidenceOf(s_wifi[i]), flk::kScoreWifi, s_wifi[i].lastMs);
  for (int i = 0; i < s_bleN; ++i) recordMatch(s_ble[i].mac, s_ble[i].name, s_ble[i].rssi, bleEvidence(s_ble[i]), flk::kScoreBle, s_ble[i].lastMs);
  s_lastScore = flock_score::compute(s_wifiN > 0, s_bleN > 0, 0, 0, false);
}
static void selectPhase(Phase phase) { s_phase = phase; s_phaseEntered = false; }
static void finishCycle() {
  scoreAndAlert();
  selectPhase(s_mode == 2 ? P_BLE : P_WIFI);
}
static void afterWifi() {
  if (s_mode != 1 && s_settings[1]) selectPhase(P_BLE);
  else if (s_mode == 0) selectPhase(P_SENSORS);
  else finishCycle();
}
static void runPhase() {
  // Start once on entry, regardless of how much setup/painting delayed this tick.
  if (!s_phaseEntered) {
    s_phaseEntered = true; s_phaseStart = millis(); s_phaseOk = true;
    switch (s_phase) {
      case P_WIFI: s_phaseOk = flock_wifi::startScan(); break;
      case P_PROBE: s_phaseOk = flock_wifi::startProbeWindow(s_settings[0]); break;
      case P_BLE: s_phaseOk = s_settings[1] && flock_ble::startWindow(); break;
      case P_SENSORS:
        s_nrfAct = flock_sensors::sampleNrf();
        s_lteStr = flock_sensors::sampleLte();
        break;
    }
  }
  const uint32_t elapsed = uint32_t(millis() - s_phaseStart);
  switch (s_phase) {
    case P_WIFI: {
      int matched = 0;
      const bool complete = flock_wifi::pollScan(&matched);
      if (!s_phaseOk || complete || elapsed >= flk::kWifiWindowMs + 4000) {
        if (!complete && s_phaseOk) flock_wifi::stop();
        selectPhase(P_PROBE);
      }
      break;
    }
    case P_PROBE:
      flock_wifi::pollProbes();
      if (!s_phaseOk || elapsed >= flk::kProbeWindowMs) {
        flock_wifi::stopProbeWindow(); afterWifi();
      }
      break;
    case P_BLE:
      flock_ble::poll();
      if ((!s_phaseOk && elapsed >= 1000) || !s_settings[1] || elapsed >= flk::kBleWindowMs ||
          (s_phaseOk && !flock_ble::running())) {
        flock_ble::stopWindow();
        if (s_mode == 2) finishCycle(); else selectPhase(P_SENSORS);
      }
      break;
    case P_SENSORS: finishCycle(); break;
  }
}
static const char* phaseName() {
  switch (s_phase) { case P_WIFI: return "AP SCAN"; case P_PROBE: return "PROBES"; case P_BLE: return "BLE RX"; default: return "RF SAMPLE"; }
}
static void health() {
  char line[64];
  if (s_mode == 2) snprintf(line, sizeof(line), "WiFi disabled in BLE-only mode");
  else snprintf(line, sizeof(line), "APs %d  probes %lu  matches %d", flock_wifi::apsSeen(), (unsigned long)flock_wifi::probeFramesSeen(), s_wifiN);
  text(line, 8, 47, UI_DIM_TEXT);
  if (s_mode == 1 || !s_settings[1]) snprintf(line, sizeof(line), "BLE disabled%s", s_mode == 1 ? " in WiFi-only mode" : " in settings");
  else snprintf(line, sizeof(line), "BLE reports %lu  matches %d", (unsigned long)flock_ble::advertisementsSeen(), s_bleN);
  text(line, 8, 59, UI_DIM_TEXT);
  const int wifiError = s_mode == 2 ? 0 : flock_wifi::lastError();
  const int bleError = s_mode == 1 || !s_settings[1] ? 0 : flock_ble::lastError();
  if (wifiError || bleError) snprintf(line, sizeof(line), "RX error WiFi:%d BLE:%d", wifiError, bleError);
  else snprintf(line, sizeof(line), "%s  CH %02u | passive", phaseName(), flock_wifi::channel());
  text(line, 8, 71, wifiError || bleError ? UI_WARN : UI_ICON);
}
static void drawDetect() {
  health();
  const int count = totalHits();
  if (!count) {
    text("No matching candidates yet", 8, 106, UI_TEXT, 2);
    text("Seen counts include nonmatches.", 8, 133, UI_DIM_TEXT);
    text("A quiet scan does not exclude cameras.", 8, 148, UI_DIM_TEXT);
    return;
  }
  const int rows = max(1, (contentBottom() - kListTop - 16) / 38);
  const int first = (s_cursor / rows) * rows;
  for (int i = first; i < count && i < first + rows; ++i) {
    const int y = kListTop + (i-first)*38; const bool selected = i == s_cursor;
    const bool wifi = i < s_wifiN; const int bi = i-s_wifiN;
    const char* name = wifi ? s_wifi[i].ssid : s_ble[bi].name;
    const char* mac = wifi ? s_wifi[i].mac : s_ble[bi].mac;
    const int rssi = wifi ? s_wifi[i].rssi : s_ble[bi].rssi;
    panel(6, y, tft.width()-12, 36, selected);
    const uint16_t fg = selected ? TFT_BLACK : UI_TEXT;
    text(name[0] ? name : (wifi ? "WiFi candidate" : "BLE candidate"), 12, y+3, fg, 1, 216);
    text(mac, 12, y+14, fg, 1, 124);
    text(String(rssi)+" dBm", 174, y+14, fg, 1, 56);
    text(wifi ? confidenceOf(s_wifi[i]) : bleEvidence(s_ble[bi]), 12, y+25, selected ? TFT_BLACK : UI_DIM_TEXT);
  }
  text(String(s_cursor+1)+" / "+String(count)+"  unverified candidates", 8, contentBottom()-12, UI_DIM_TEXT);
}
static void drawMeter() {
  health();
  if (!totalHits()) { text("No matching signal yet", 8, 110, UI_TEXT, 2); return; }
  const bool wifi = s_cursor < s_wifiN; const int bi = s_cursor-s_wifiN;
  const char* name = wifi ? s_wifi[s_cursor].ssid : s_ble[bi].name;
  const char* mac = wifi ? s_wifi[s_cursor].mac : s_ble[bi].mac;
  const int rssi = wifi ? s_wifi[s_cursor].rssi : s_ble[bi].rssi;
  text(name[0] ? name : mac, 8, 96, UI_TEXT, 2);
  text(mac, 8, 122, UI_DIM_TEXT);
  text(String(rssi)+" dBm", 8, 147, UI_ICON, 2);
  meter(8, 174, tft.width()-16, (rssi+100)*100/65);
  text("Signal strength is not distance.", 8, 199, UI_DIM_TEXT);
  text("Prev / Next chooses a candidate.", 8, 214, UI_DIM_TEXT);
}
static void drawSensors() {
  text("RF ACTIVITY / optional modules", 8, 50, UI_ICON);
  text(s_nrfAct < 0 ? "nRF24: unavailable / not sampled" : "nRF24: "+String(s_nrfAct)+"% sampled occupancy", 8, 77);
  meter(8, 94, tft.width()-16, max(0, s_nrfAct));
  text(s_lteStr < 0 ? "CC1101: unavailable / not sampled" : "SubGHz energy index: "+String(s_lteStr), 8, 124);
  meter(8, 141, tft.width()-16, max(0, s_lteStr));
  text("Spots: 850 / 868 / 895 MHz", 8, 159, UI_DIM_TEXT);
  text("IR: raw photodiode not fitted", 8, 188, UI_DIM_TEXT);
  text("RF energy cannot identify a camera.", 8, 212, UI_DIM_TEXT);
}
static void drawScore() {
  text("SESSION EVIDENCE", 8, 50, UI_ICON);
  text("WiFi candidates: "+String(s_wifiN), 8, 80, UI_TEXT, 2);
  text("BLE candidates: "+String(s_bleN), 8, 106, UI_TEXT, 2);
  text(flock_score::label(s_lastScore), 8, 144, UI_ICON);
  text("Matches are clues, not confirmation.", 8, 172, UI_DIM_TEXT);
  text("WiFi/BLE matches may be unrelated.", 8, 187, UI_DIM_TEXT);
  text("Ambient RF adds no identity evidence.", 8, 202, UI_DIM_TEXT);
}
static void drawGps() {
  text("OBSERVER LOCATION", 8, 50, UI_ICON);
  if (!flock_gps::fixValid()) {
    text(s_settings[2] ? "No fresh GPS fix" : "GPS disabled", 8, 88, UI_WARN, 2);
    text("Check GPS wiring and satellite view.", 8, 119, UI_DIM_TEXT); return;
  }
  char line[48];
  snprintf(line, sizeof(line), "Latitude  %.6f", double(flock_gps::lat())); text(line, 8, 82);
  snprintf(line, sizeof(line), "Longitude %.6f", double(flock_gps::lon())); text(line, 8, 103);
  char time[10]; flock_gps::utc(time, sizeof(time)); text(String("UTC ")+time, 8, 130);
  text("Logs store your receiver position,", 8, 169, UI_DIM_TEXT);
  text("not the camera's coordinates.", 8, 184, UI_DIM_TEXT);
  text("Fix expires after 5 seconds unseen.", 8, 210, UI_DIM_TEXT);
}
static void drawLog() {
  const int count = flock_log::historyCount();
  text("Recent session records: "+String(count)+" / 20", 8, 48, UI_ICON);
  text(!flock_log::ok() ? flock_log::lastError() : s_settings[4] ? "SD autolog ON" : "SD autolog OFF; RAM history active", 8, 62, flock_log::ok() ? UI_DIM_TEXT : UI_WARN);
  text("Export includes GPS-tagged rows only.", 8, 76, UI_DIM_TEXT);
  const int rows = max(1, (contentBottom()-100) / 34);
  for (int k = 0; k < rows && s_cursor+k < count; ++k) {
    const auto* r = flock_log::history(count-1-s_cursor-k); const int y = 96+k*34;
    panel(6, y, tft.width()-12, 32, false);
    text(String(r->utc)+"  "+r->mac, 12, y+3);
    text(String(r->confidence)+"  "+String(r->rssi)+" dBm", 12, y+16, UI_DIM_TEXT);
  }
}
static void drawSett() {
  static const char* names[5] = {"Probe channel hop", "BLE reception", "GPS receiver", "Match sound", "SD autolog"};
  for (int i = 0; i < 5; ++i) {
    const int y = 48+i*32; const bool sel = s_cursor == i;
    panel(6, y, tft.width()-12, 30, sel);
    text(names[i], 12, y+7, sel ? TFT_BLACK : UI_TEXT, 1, 170);
    text(s_settings[i] ? "ON" : "OFF", 201, y+7, sel ? TFT_BLACK : UI_ICON, 1, 29);
  }
  text("Next selects; Toggle changes setting.", 8, 218, UI_DIM_TEXT);
}
static void paint() {
  refreshSnapshots();
  tft.fillRect(0, kContentTop, tft.width(), contentBottom()-kContentTop, TFT_BLACK);
  tabHeader();
  switch (s_tab) {
    case T_DETECT: drawDetect(); break; case T_METER: drawMeter(); break;
    case T_SENSORS: drawSensors(); break; case T_SCORE: drawScore(); break;
    case T_GPS: drawGps(); break; case T_LOG: drawLog(); break; case T_SETT: drawSett(); break;
    default: break;
  }
}
static void settingsIo(bool write) {
  static const char* keys[5] = {"hop", "ble", "gps", "snd", "ward"};
  if (!s_prefs.begin("flock", !write)) return;
  for (int i = 0; i < 5; ++i) {
    if (write) s_prefs.putBool(keys[i], s_settings[i]);
    else s_settings[i] = s_prefs.getBool(keys[i], true);
  }
  s_prefs.end();
}
static void loadOuis() {
  if (!flock_log::ok()) return;
  File file = SD.open("/flock/ouis.txt"); if (!file) return;
  while (file.available()) {
    String value = file.readStringUntil('\n'); value.trim(); value.toLowerCase();
    if (value.length() == 8) flock_wifi::addOui(value.c_str());
  }
  file.close();
}
void openAt(uint8_t tab, uint8_t mode) {
  s_tab = mode == 3 ? T_METER : Tab(tab < T_COUNT ? tab : 0);
  s_mode = mode <= 2 ? mode : 0; s_cursor = 0;
}
void appSetup() {
  feature_active = true;
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true); featureClearContent(TFT_BLACK);
  setStatusTitle("FLOCK"); drawStatusBar(readBatteryVoltage(), true); setupTouchscreen();
  settingsIo(false);
  flock_wifi::begin(); flock_ble::begin(); flock_sensors::begin();
  if (s_settings[2]) flock_gps::begin(); else flock_gps::end();
  flock_log::begin(); loadOuis();
  s_nrfAct = s_lteStr = -1; s_lastScore = 0; s_wifiN = s_bleN = 0;
  s_lastPaint = 0; s_started = true;
  selectPhase(s_mode == 2 ? P_BLE : P_WIFI);
  nav(); paint();
  Serial.printf("[flock] opened view=%u mode=%u\n", unsigned(s_tab), unsigned(s_mode));
}
static void changeTab(int direction) {
  s_tab = s_tab == T_METER ? T_DETECT : Tab((int(s_tab)+T_COUNT+direction)%T_COUNT);
  s_cursor = 0; nav(); paint();
}
void appLoop() {
  if (!s_started) return;
  if (feature_exit_requested || featureExitButtonPressed()) { feature_exit_requested = true; exit(); return; }
  if (isButtonPressedEdge(BTN_LEFT)) changeTab(-1);
  else if (isButtonPressedEdge(BTN_RIGHT)) changeTab(1);
  else if (isButtonPressedEdge(BTN_DOWN)) {
    if (s_tab == T_LOG) {
      const bool written = flock_log::exportDeFlock();
      showNotification(written ? "EXPORTED" : "EXPORT FAILED", written ? "/flock/deflock.csv" : flock_log::lastError());
    } else if (s_tab == T_SETT) s_cursor = (s_cursor+1)%5;
    else if (totalHits()) s_cursor = (s_cursor+1)%totalHits();
    paint();
  } else if (isButtonPressedEdge(BTN_UP)) {
    if (s_tab == T_SETT) {
      s_settings[s_cursor] = !s_settings[s_cursor]; settingsIo(true);
      if (s_cursor == 2) { if (s_settings[2]) flock_gps::begin(); else flock_gps::end(); }
      if (s_cursor == 1 && !s_settings[1]) flock_ble::stopWindow();
      if (s_cursor == 0 && s_phase == P_PROBE) { flock_wifi::stopProbeWindow(); selectPhase(P_PROBE); }
    } else if (s_tab == T_LOG) {
      const int count = flock_log::historyCount(); if (count) s_cursor = (s_cursor+1)%count;
    } else if (totalHits()) s_cursor = (s_cursor+totalHits()-1)%totalHits();
    paint();
  }
  if (s_settings[2]) flock_gps::poll();
  runPhase();
  static uint32_t lastHealthMs = 0;
  if (uint32_t(millis() - lastHealthMs) >= 5000) {
    lastHealthMs = millis();
    Serial.printf("[flock] phase=%s APs=%d probes=%lu BLE=%lu matches=%d/%d errors=%d/%d\n",
      phaseName(), flock_wifi::apsSeen(), (unsigned long)flock_wifi::probeFramesSeen(),
      (unsigned long)flock_ble::advertisementsSeen(), s_wifiN, s_bleN,
      flock_wifi::lastError(), flock_ble::lastError());
  }
  maintainTouchNavBar(); updateStatusBar();
  if (uint32_t(millis()-s_lastPaint) >= 600) { s_lastPaint = millis(); scoreAndAlert(); paint(); }
  delay(15);
}
void exit() {
  if (!s_started) return;
  flock_wifi::stop(); flock_ble::stopWindow(); flock_gps::end();
  ledcWriteTone(1, 0); s_started = false;
  Serial.println("[flock] closed");
}
} // namespace flock_menu
