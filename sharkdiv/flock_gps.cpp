// flock_gps.cpp — minimal GGA/RMC parser on the GPS UART (own instance;
// only one feature runs at a time on this firmware).
#include "flock_gps.h"
#include "flock_config.h"
#include "gps.h"
#ifndef GPS_UART_BAUD
#define GPS_UART_BAUD 9600
#endif

#include <HardwareSerial.h>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace flock_gps {

static HardwareSerial s_gps(1);
static char s_line[96];
static int s_len = 0;
static float s_lat = NAN, s_lon = NAN;
static char s_utc[10] = "";
static uint32_t s_fixMs = 0;

// follow tracking: per-MAC bounding fixes
struct Follow { char mac[18]; float lats[4]; float lons[4]; int n; };
static Follow s_follow[8];
static int s_followCount = 0;

static float nmeaToDeg(const char* v, const char* hemi) {
  if (!v || !*v) return NAN;
  const float raw = atof(v);
  const int deg = (int)(raw / 100.0f);
  const float minutes = raw - deg * 100.0f;
  float d = deg + minutes / 60.0f;
  if (hemi && (*hemi == 'S' || *hemi == 'W')) d = -d;
  return d;
}

static void parseLine(const char* l) {
  if (!strncmp(l, "$G", 2) && (strstr(l, "GGA") || strstr(l, "RMC"))) {
    char copy[96];
    strncpy(copy, l, 95); copy[95] = 0;
    // GGA: $..GGA,time,lat,NS,lon,EW,fix,...
    char* p = strtok(copy, ",");
    for (int f = 0; p && f < 9; f++, p = strtok(nullptr, ",")) {
      if (f == 1) strncpy(s_utc, p, 8), s_utc[8] = 0;
      if (f == 2 && strlen(p) > 5) s_lat = nmeaToDeg(p, nullptr);
      if (f == 3 && s_lat == s_lat) { if (*p == 'S') s_lat = -s_lat; }
      if (f == 4 && strlen(p) > 5) s_lon = nmeaToDeg(p, nullptr);
      if (f == 5 && s_lon == s_lon) { if (*p == 'W') s_lon = -s_lon; }
      if (f == 6) { if (atoi(p) > 0) s_fixMs = millis(); }
    }
  }
}

void begin() {
  s_len = 0;
  s_fixMs = 0;
  s_lat = NAN; s_lon = NAN;
  s_gps.end();
  s_gps.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
}

void poll() {
  while (s_gps.available()) {
    const char c = (char)s_gps.read();
    if (c == '\n') {
      s_line[s_len] = 0;
      parseLine(s_line);
      s_len = 0;
    } else if (c != '\r' && s_len < (int)sizeof(s_line) - 1) {
      s_line[s_len++] = c;
    }
  }
}

bool fixValid() { return s_fixMs != 0 && s_lat == s_lat && s_lon == s_lon; }
float lat() { return s_lat; }
float lon() { return s_lon; }
void utc(char* out, int cap) {
  snprintf(out, cap, "%s", s_utc[0] ? s_utc : "--:--:--");
}

float distanceM(float lat1, float lon1, float lat2, float lon2) {
  const float dLat = (lat2 - lat1) * 111194.9f;
  const float dLon = (lon2 - lon1) * 111194.9f *
                     cosf((lat1 + lat2) * 0.5f * 0.01745329f);
  return sqrtf(dLat * dLat + dLon * dLon);
}

bool followAlert(const char* mac) {
  Follow* f = nullptr;
  for (int i = 0; i < s_followCount; i++) {
    if (!strcmp(s_follow[i].mac, mac)) { f = &s_follow[i]; break; }
  }
  if (!f) {
    if (s_followCount >= 8) s_followCount = 0;   // ring reuse
    f = &s_follow[s_followCount++];
    strncpy(f->mac, mac, 17); f->mac[17] = 0;
    f->n = 0;
  }
  if (!fixValid()) return false;
  // store up to 4 distinct positions (>100 m apart)
  bool farEnough = true;
  for (int i = 0; i < f->n; i++) {
    if (distanceM(f->lats[i], f->lons[i], lat(), lon()) < 100.0f) {
      farEnough = false;
    }
  }
  if (farEnough && f->n < 4) {
    f->lats[f->n] = lat();
    f->lons[f->n] = lon();
    f->n++;
  }
  if (f->n < flk::kFollowFixes) return false;
  // any pair further than the threshold?
  for (int i = 0; i < f->n; i++) {
    for (int j = i + 1; j < f->n; j++) {
      if (distanceM(f->lats[i], f->lons[i], f->lats[j], f->lons[j])
          > flk::kFollowMinM) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace flock_gps
