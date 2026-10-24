#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Bounded NMEA parser shared with host regression tests. Empty fields retain
// their positions and a valid checksum is required before a fix can change.
namespace FlockGpsParser {
struct Fix { float latitude = NAN, longitude = NAN; char utc[10] = ""; };
enum Result { Ignored, NoFix, ValidFix };
inline int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
inline bool coordinate(const char* text, const char* hemi, bool latitude, float& out) {
  if (!text || !*text || !hemi || !hemi[0] || hemi[1]) return false;
  if (latitude ? (hemi[0] != 'N' && hemi[0] != 'S') : (hemi[0] != 'E' && hemi[0] != 'W')) return false;
  char* end;
  const double raw = strtod(text, &end);
  if (*end || !std::isfinite(raw) || raw < 0 || raw > (latitude ? 9000 : 18000)) return false;
  const int degrees = static_cast<int>(raw / 100.0);
  const double minutes = raw - degrees * 100.0;
  if (minutes >= 60 || degrees > (latitude ? 90 : 180) ||
      (degrees == (latitude ? 90 : 180) && minutes != 0)) return false;
  out = static_cast<float>(degrees + minutes / 60.0);
  if (hemi[0] == 'S' || hemi[0] == 'W') out = -out;
  return true;
}
inline Result parse(const char* sentence, Fix& fix) {
  if (!sentence || sentence[0] != '$' || strlen(sentence) >= 96) return Ignored;
  const char* star = strchr(sentence, '*');
  if (!star || strlen(star) != 3 || hex(star[1]) < 0 || hex(star[2]) < 0) return Ignored;
  unsigned checksum = 0;
  for (const char* p = sentence + 1; p < star; ++p) checksum ^= static_cast<unsigned char>(*p);
  if (checksum != unsigned(hex(star[1]) * 16 + hex(star[2]))) return Ignored;
  char copy[96];
  const size_t length = size_t(star - sentence);
  memcpy(copy, sentence, length); copy[length] = 0;
  char* fields[20]; int count = 1; fields[0] = copy;
  for (char* p = copy; *p && count < 20; ++p) if (*p == ',') { *p = 0; fields[count++] = p + 1; }
  if (strlen(fields[0]) != 6) return Ignored;
  const bool gga = !strcmp(fields[0] + 3, "GGA");
  const bool rmc = !strcmp(fields[0] + 3, "RMC");
  if ((!gga && !rmc) || count < 7) return Ignored;
  const char* time = fields[1];
  if (strlen(time) >= 6) {
    bool digits = true; for (int i = 0; i < 6; ++i) digits &= time[i] >= '0' && time[i] <= '9';
    const int h = (time[0]-'0')*10 + time[1]-'0';
    const int m = (time[2]-'0')*10 + time[3]-'0';
    const int s = (time[4]-'0')*10 + time[5]-'0';
    if (digits && h < 24 && m < 60 && s < 60) snprintf(fix.utc, sizeof(fix.utc), "%02d:%02d:%02d", h, m, s);
  }
  if (gga ? (fields[6][0] < '1' || fields[6][0] > '8' || fields[6][1]) : strcmp(fields[2], "A")) return NoFix;
  const int lat = gga ? 2 : 3, lon = gga ? 4 : 5;
  float latitude, longitude;
  if (!coordinate(fields[lat], fields[lat+1], true, latitude) ||
      !coordinate(fields[lon], fields[lon+1], false, longitude)) return NoFix;
  fix.latitude = latitude; fix.longitude = longitude;
  return ValidFix;
}
} // namespace FlockGpsParser
