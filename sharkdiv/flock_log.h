#pragma once
// flock_log.h — CSV/JSON/KML + DeFlock export to SD, session history,
// and the 30-second re-alert suppression.
#include <Arduino.h>

namespace flock_log {

struct Record {
  char mac[18];
  char ssid[33];
  int   rssi;
  int   score;
  const char* confidence;
  float lat, lon;
  char  utc[10];
  bool  follow;
};

void begin();                       // mount SD, open session files
bool ok();
void add(const Record& r, bool persist = true); // RAM history even without SD
int  historyCount();
const Record* history(int i);
bool canAlert(const char* mac, uint32_t observedMs); // fresh reception, 30 s suppression
bool exportDeFlock();               // valid GPS rows only; false reports failure
const char* lastError();

}  // namespace flock_log
