#pragma once
// flock_ble.h — passive BLE scan for Flock/Raven advertisement names.
#include <Arduino.h>

namespace flock_ble {

struct Hit {
  char name[24];
  char mac[18];
  int8_t rssi;
  uint32_t lastMs;
};
constexpr int kMaxHits = 8;

void begin();
void startWindow();          // non-blocking passive scan
void stopWindow();
// Copy of the matched devices, strongest first; entries unseen for
// flk::kHitTtlMs are dropped. Safe to call from the UI.
int  snapshot(Hit* out, int max);
int  matches();              // matched devices currently in the list
int  bestRssi();
bool running();
void poll();          // callback-driven; kept for the engine

}  // namespace flock_ble
