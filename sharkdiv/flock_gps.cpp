#include "flock_gps.h"
#include "flock_config.h"
#include "FlockGpsParser.h"
#include <HardwareSerial.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#ifndef GPS_UART_BAUD
#define GPS_UART_BAUD 9600
#endif

namespace flock_gps {
static HardwareSerial s_gps(1);
static char s_line[96];
static size_t s_len = 0;
static bool s_overflow = false, s_enabled = false, s_haveFix = false;
static FlockGpsParser::Fix s_fix;
static uint32_t s_fixMs = 0;
struct Follow { char mac[18]; float lats[4], lons[4]; int n; };
static Follow s_follow[8];
static int s_followCount = 0, s_followNext = 0;

void begin() {
  s_len = 0; s_overflow = false; s_haveFix = false;
  s_fix = FlockGpsParser::Fix{};
  s_gps.end();
  s_gps.begin(GPS_UART_BAUD, SERIAL_8N1, GPS_UART_RX, GPS_UART_TX);
  s_enabled = true;
}
void end() { s_gps.end(); s_enabled = s_haveFix = false; }
void poll() {
  if (!s_enabled) return;
  // Bound one UI iteration even when a receiver produces continuous data.
  for (int bytes = 0; bytes < 512 && s_gps.available(); ++bytes) {
    const char c = char(s_gps.read());
    if (c == '$') { s_len = 0; s_overflow = false; }
    if (c == '\n') {
      if (!s_overflow) {
        s_line[s_len] = 0;
        const auto result = FlockGpsParser::parse(s_line, s_fix);
        if (result == FlockGpsParser::ValidFix) { s_haveFix = true; s_fixMs = millis(); }
        else if (result == FlockGpsParser::NoFix) s_haveFix = false;
      }
      s_len = 0; s_overflow = false;
    } else if (c != '\r') {
      if (s_len < sizeof(s_line) - 1) s_line[s_len++] = c;
      else s_overflow = true;
    }
  }
}
bool fixValid() { return s_enabled && s_haveFix && uint32_t(millis() - s_fixMs) < 5000; }
float lat() { return fixValid() ? s_fix.latitude : NAN; }
float lon() { return fixValid() ? s_fix.longitude : NAN; }
void utc(char* out, int cap) {
  if (out && cap > 0) snprintf(out, cap, "%s", s_enabled && s_fix.utc[0] ? s_fix.utc : "--:--:--");
}
float distanceM(float lat1, float lon1, float lat2, float lon2) {
  const float dy = (lat2 - lat1) * 111194.9f;
  const float dx = (lon2 - lon1) * 111194.9f * cosf((lat1 + lat2) * 0.5f * 0.01745329f);
  return sqrtf(dx*dx + dy*dy);
}
bool followAlert(const char* mac) {
  if (!mac || !*mac || !fixValid()) return false;
  Follow* f = nullptr;
  for (int i = 0; i < s_followCount; ++i) if (!strcmp(s_follow[i].mac, mac)) { f = &s_follow[i]; break; }
  if (!f) {
    const int slot = s_followCount < 8 ? s_followCount++ : s_followNext;
    if (s_followCount == 8) s_followNext = (slot + 1) % 8;
    f = &s_follow[slot]; *f = Follow{};
    snprintf(f->mac, sizeof(f->mac), "%s", mac);
  }
  bool distinct = true;
  for (int i = 0; i < f->n; ++i) if (distanceM(f->lats[i], f->lons[i], lat(), lon()) < 100) distinct = false;
  if (distinct) {
    if (f->n == 4) {
      memmove(f->lats, f->lats + 1, 3 * sizeof(float));
      memmove(f->lons, f->lons + 1, 3 * sizeof(float));
      f->n = 3;
    }
    f->lats[f->n] = lat(); f->lons[f->n++] = lon();
  }
  if (f->n < flk::kFollowFixes) return false;
  for (int i = 0; i < f->n; ++i) for (int j = i + 1; j < f->n; ++j)
    if (distanceM(f->lats[i], f->lons[i], f->lats[j], f->lons[j]) > flk::kFollowMinM) return true;
  return false;
}
} // namespace flock_gps
