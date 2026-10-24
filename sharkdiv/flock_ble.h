#pragma once
// flock_ble.h — passive BLE scan for Flock/Raven advertisement names.
#include <Arduino.h>

namespace flock_ble {

struct Hit {
  char name[24];
  char mac[18];
  int8_t rssi;
  uint8_t evidence; // FlockSignatures::BleEvidence flags; candidate only
  uint32_t lastMs;
};
constexpr int kMaxHits = 8;

void begin();
void setMatchSound(bool enabled); // per-tool mute; global mute also applies
bool startWindow();          // non-blocking passive scan
void stopWindow();
// Copy of the matched devices, strongest first; entries unseen for
// flk::kHitTtlMs are dropped. Safe to call from the UI.
int  snapshot(Hit* out, int max);
int  matches();              // matched devices currently in the list
int  bestRssi();
bool running();
int lastError();
uint32_t advertisementsSeen();
void poll();          // callback-driven; kept for the engine

}  // namespace flock_ble
