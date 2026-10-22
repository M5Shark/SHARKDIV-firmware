#pragma once
// flock_ble.h — passive BLE scan for Flock/Raven advertisement names.
namespace flock_ble {
void begin();
void startWindow();          // non-blocking passive scan
void stopWindow();
int  matches();              // matched devices in last window
int  bestRssi();
bool running();
void poll();          // callback-driven; kept for the engine
}  // namespace flock_ble
