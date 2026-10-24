#include "flock_log.h"
#include "flock_config.h"
#include "utils.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <FS.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace flock_log {
static constexpr int kHist = 20;
static Record s_hist[kHist];
static int s_histCount = 0;
static bool s_ok = false;
static char s_err[64] = "";
struct AlertT { char mac[18]; uint32_t ms, observedMs; };
static AlertT s_alerts[16];
static int s_alertCount = 0;

static bool fail(const char* message) {
  snprintf(s_err, sizeof(s_err), "%s", message); s_ok = false; return false;
}
void begin() {
  // Launcher tools share the same boot session; opening Log must not erase it.
  s_ok = false; s_err[0] = 0;
  sdSpiInit();
  if (!sdMountChipSelect(SD_CS)) { fail("SD unavailable; history is in RAM"); return; }
  if (!SD.exists("/flock") && !SD.mkdir("/flock")) { fail("Cannot create /flock"); return; }
  s_ok = true;
}
bool ok() { return s_ok; }
const char* lastError() { return s_err; }

static String csv(const char* text) {
  String value = "\"";
  if (text) for (const char* p = text; *p; ++p) {
    if (*p == '"') value += '"';
    value += (*p == '\r' || *p == '\n') ? ' ' : *p;
  }
  value += '"'; return value;
}
static bool coordinates(const Record& r) { return std::isfinite(r.lat) && std::isfinite(r.lon); }
static String coordinate(float value) { return std::isfinite(value) ? String(value, 6) : String(""); }
static bool writeLine(File& file, const String& line) {
  return file.println(line) == line.length() + 2 && file.getWriteError() == 0;
}
static bool writeKml() {
  // A complete session document, never a stream of unclosed XML fragments.
  File file = SD.open("/flock/log.kml", FILE_WRITE);
  if (!file) return fail("Cannot write log.kml");
  bool good = writeLine(file, "<?xml version=\"1.0\" encoding=\"UTF-8\"?><kml xmlns=\"http://www.opengis.net/kml/2.2\"><Document>");
  for (int i = 0; i < s_histCount && good; ++i) {
    const Record& r = s_hist[i]; if (!coordinates(r)) continue;
    // MAC, UTC and numeric fields are controlled values; no radio-supplied XML.
    String line = "<Placemark><name>Unverified candidate "; line += r.mac;
    line += "</name><description>Observed at "; line += r.utc;
    line += " RSSI "; line += r.rssi;
    line += " dBm</description><Point><coordinates>";
    line += coordinate(r.lon); line += ','; line += coordinate(r.lat);
    line += ",0</coordinates></Point></Placemark>";
    good = writeLine(file, line);
  }
  good = writeLine(file, "</Document></kml>") && good;
  file.close();
  return good ? true : fail("Incomplete KML write");
}
void add(const Record& input, bool persist) {
  Record r = input;
  r.mac[17] = 0; r.ssid[32] = 0; r.utc[9] = 0;
  if (!r.confidence) r.confidence = "CANDIDATE";
  if (s_histCount < kHist) s_hist[s_histCount++] = r;
  else { memmove(s_hist, s_hist + 1, sizeof(Record) * (kHist - 1)); s_hist[kHist - 1] = r; }
  if (!persist || !s_ok) return;
  File file = SD.open("/flock/log.csv", FILE_APPEND);
  if (!file) { fail("Cannot append log.csv"); return; }
  bool good = true;
  if (!file.size()) good = writeLine(file, "mac,ssid,rssi,score,evidence,latitude,longitude,utc,repeated");
  String line = csv(r.mac) + ',' + csv(r.ssid) + ',' + String(r.rssi) + ',' + String(r.score) + ',' + csv(r.confidence) + ',' + coordinate(r.lat) + ',' + coordinate(r.lon) + ',' + csv(r.utc) + ',' + (r.follow ? "1" : "0");
  good = writeLine(file, line) && good; file.close();
  if (!good) { fail("Incomplete CSV write"); return; }

  file = SD.open("/flock/log.json", FILE_APPEND);
  if (!file) { fail("Cannot append log.json"); return; }
  StaticJsonDocument<512> doc;
  doc["mac"] = r.mac; doc["ssid"] = r.ssid; doc["rssi"] = r.rssi;
  doc["score"] = r.score; doc["evidence"] = r.confidence;
  if (coordinates(r)) { doc["lat"] = r.lat; doc["lon"] = r.lon; }
  else { doc["lat"] = nullptr; doc["lon"] = nullptr; }
  doc["time"] = r.utc; doc["repeated"] = r.follow;
  good = serializeJson(doc, file) == measureJson(doc);
  good = file.println() == 2 && file.getWriteError() == 0 && good;
  file.close();
  if (!good) { fail("Incomplete JSON write"); return; }
  writeKml();
}
int historyCount() { return s_histCount; }
const Record* history(int i) { return i >= 0 && i < s_histCount ? &s_hist[i] : nullptr; }
bool canAlert(const char* mac, uint32_t observedMs) {
  const uint32_t now = millis();
  // Retained history is display-only: never geotag an old observation at a
  // newly acquired location, or use it to manufacture repeated detections.
  if (uint32_t(now - observedMs) > 2000) return false;
  for (int i = 0; i < s_alertCount; ++i) if (!strcmp(s_alerts[i].mac, mac)) {
    if (int32_t(observedMs - s_alerts[i].observedMs) <= 0) return false;
    if (uint32_t(now - s_alerts[i].ms) < flk::kRealertMs) return false;
    s_alerts[i].ms = now; s_alerts[i].observedMs = observedMs; return true;
  }
  int slot = s_alertCount;
  if (s_alertCount < 16) ++s_alertCount;
  else {
    slot = 0;
    for (int i = 1; i < 16; ++i) if (uint32_t(now - s_alerts[i].ms) > uint32_t(now - s_alerts[slot].ms)) slot = i;
  }
  snprintf(s_alerts[slot].mac, sizeof(s_alerts[slot].mac), "%s", mac);
  s_alerts[slot].ms = now; s_alerts[slot].observedMs = observedMs; return true;
}
bool exportDeFlock() {
  if (!s_ok) return false;
  int located = 0;
  for (int i = 0; i < s_histCount; ++i) if (coordinates(s_hist[i])) ++located;
  if (!located) { snprintf(s_err, sizeof(s_err), "No GPS-tagged records to export"); return false; }
  File file = SD.open("/flock/deflock.csv", FILE_WRITE);
  if (!file) return fail("Cannot write deflock.csv");
  bool good = writeLine(file, "latitude,longitude,title,notes");
  for (int i = 0; i < s_histCount && good; ++i) {
    const Record& r = s_hist[i]; if (!coordinates(r)) continue;
    String notes = String(r.mac) + " " + r.confidence + " " + String(r.rssi) + " dBm; observer location, unverified";
    good = writeLine(file, coordinate(r.lat) + ',' + coordinate(r.lon) + ",\"Unverified camera candidate\"," + csv(notes.c_str()));
  }
  file.close();
  if (!good) return fail("Incomplete export write");
  s_err[0] = 0; return true;
}
} // namespace flock_log
