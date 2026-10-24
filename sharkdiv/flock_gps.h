#pragma once
// flock_gps.h — geotagging for detections + follow-alert tracking.
namespace flock_gps {
void begin();
void end();                       // release UART and invalidate the old fix
void poll();                       // feed NMEA, keep last fix fresh
bool fixValid();
float lat();
float lon();
void utc(char* out, int cap);      // "HH:MM:SS"
bool followAlert(const char* mac); // true when MAC seen 3+ places >500m apart
float distanceM(float lat1, float lon1, float lat2, float lon2);
}  // namespace flock_gps
