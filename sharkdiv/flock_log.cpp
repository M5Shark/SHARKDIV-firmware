// flock_log.cpp — see header. Files under /flock/ on the SD card.
#include "flock_log.h"
#include "flock_config.h"
#include "utils.h"
#include "shared.h"
#include <SD.h>
#include <FS.h>
#include <cstdio>
#include <cstring>

namespace flock_log {

static constexpr int kHist = 20;
static Record s_hist[kHist];
static int s_histCount = 0;
static bool s_ok = false;
static char s_err[48] = "";

struct AlertT { char mac[18]; uint32_t ms; };
static AlertT s_alerts[16];
static int s_alertCount = 0;

static void ensureDir() {
  if (!SD.exists("/flock")) SD.mkdir("/flock");
}

void begin() {
  s_ok = false;
  s_histCount = 0;
  s_err[0] = 0;
  sdSpiInit();
  if (!sdMountChipSelect(SD_CS)) {
    strncpy(s_err, "SD mount failed", sizeof(s_err) - 1);
    return;
  }
  ensureDir();
  s_ok = true;
}

bool ok() { return s_ok; }
const char* lastError() { return s_err; }

static void appendFile(const char* path, const char* line) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) { snprintf(s_err, sizeof(s_err), "open %s failed", path); return; }
  f.println(line);
  f.close();
}

void add(const Record& r) {
  if (!s_ok) return;

  // history ring
  if (s_histCount < kHist) {
    s_hist[s_histCount++] = r;
  } else {
    memmove(&s_hist[0], &s_hist[1], sizeof(Record) * (kHist - 1));
    s_hist[kHist - 1] = r;
  }

  char line[192];

  // CSV
  snprintf(line, sizeof(line), "%s,%s,%d,%d,%s,%.5f,%.5f,%s,%d",
           r.mac, r.ssid[0] ? r.ssid : "-", r.rssi, r.score, r.confidence,
           (double)r.lat, (double)r.lon, r.utc, r.follow ? 1 : 0);
  appendFile("/flock/log.csv", line);

  // JSON Lines
  snprintf(line, sizeof(line),
           "{\"mac\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"score\":%d,"
           "\"confidence\":\"%s\",\"lat\":%.5f,\"lon\":%.5f,"
           "\"time\":\"%s\",\"follow\":%s}",
           r.mac, r.ssid[0] ? r.ssid : "-", r.rssi, r.score, r.confidence,
           (double)r.lat, (double)r.lon, r.utc, r.follow ? "true" : "false");
  appendFile("/flock/log.json", line);

  // KML placemark
  snprintf(line, sizeof(line),
           "<Placemark><name>%s (%s)</name><description>%s rssi %d score %d"
           "</description><Point><coordinates>%.5f,%.5f,0</coordinates>"
           "</Point></Placemark>",
           r.mac, r.confidence, r.utc, r.rssi, r.score,
           (double)r.lon, (double)r.lat);
  appendFile("/flock/log.kml", line);
}

int historyCount() { return s_histCount; }
const Record* history(int i) {
  if (i < 0 || i >= s_histCount) return nullptr;
  return &s_hist[i];
}

bool canAlert(const char* mac) {
  const uint32_t now = millis();
  for (int i = 0; i < s_alertCount; i++) {
    if (!strcmp(s_alerts[i].mac, mac)) {
      if (now - s_alerts[i].ms < flk::kRealertMs) return false;
      s_alerts[i].ms = now;
      return true;
    }
  }
  if (s_alertCount < 16) {
    strncpy(s_alerts[s_alertCount].mac, mac, 17);
    s_alerts[s_alertCount].mac[17] = 0;
    s_alerts[s_alertCount].ms = now;
    s_alertCount++;
  }
  return true;
}

// DeFlock.me-style CSV: latitude, longitude, notes — rewritten per session.
void exportDeFlock() {
  if (!s_ok) return;
  File f = SD.open("/flock/deflock.csv", FILE_WRITE);
  if (!f) { strncpy(s_err, "deflock write failed", sizeof(s_err) - 1); return; }
  f.println("latitude,longitude,title,notes");
  for (int i = 0; i < s_histCount; i++) {
    const Record& r = s_hist[i];
    char line[160];
    snprintf(line, sizeof(line), "%.5f,%.5f,\"Flock camera\",\"%s %s dBm\""
             ",\"score %d via SHARKDIV\"",
             (double)r.lat, (double)r.lon, r.mac, String(r.rssi).c_str(),
             r.score);
    f.println(line);
  }
  f.close();
}

}  // namespace flock_log
