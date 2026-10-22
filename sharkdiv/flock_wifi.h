#pragma once
// flock_wifi.h — passive Wi-Fi evidence: Flock SSID/OUI matching and a
// promiscuous probe-request sniffer. Receive-only.
#include <Arduino.h>

namespace flock_wifi {

struct Hit {
  char mac[18];
  char ssid[33];
  int8_t rssi;
  bool bySsid;
  bool byOui;
  bool byProbe;       // empty-SSID probe from a matching MAC
  uint32_t lastMs;
};

void begin();
void addOui(const char* oui);   // extend OUI list (e.g. from SD)
// One async scan pass; call poll() until it returns true.
void startScan();
bool pollScan(int* matchedCount);
// Promiscuous window for empty-SSID probes from matching OUIs.
void startProbeWindow();
void stopProbeWindow();
void pollProbes();

constexpr int kMaxHits = 12;
// Copy of the current hits, strongest first; entries unseen for
// flk::kHitTtlMs are dropped. Safe to call from the UI.
int snapshot(Hit* out, int max);
int hitCount();
int bestRssi();               // strongest matched RSSI (for the meter)
bool anyConfirmed();          // SSID or OUI+probe match

}  // namespace flock_wifi
