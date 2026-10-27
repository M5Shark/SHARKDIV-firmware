#pragma once
// flock_link.h — BLE GATT bridge to the FlockFinder iOS app. The device
// runs a GATT server; detections and a live debug stream are pushed as
// JSON notifications while the app is connected. Passive detect-only.
#include <stdint.h>

namespace flock_link {

// Start/stop the GATT server (idempotent).
void start();
void stop();
bool active();

// Push a detection event: {"type":"wifi"|"ble","mac":"..","rssi":-62,
// "confidence":"CONFIRMED","ts":1234567}
void pushDetection(const char* type, const char* mac, int rssi,
                   const char* confidence);

// Push a raw debug line (truncated to fit a notification).
void pushStream(const char* line);

bool clientConnected();

}  // namespace flock_link
