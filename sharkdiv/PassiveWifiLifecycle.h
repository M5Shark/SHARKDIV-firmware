#pragma once

// Arduino core 2.x caches whether the IDF driver has started. A raw IDF stop
// does not reset that cache, and WiFi.mode(OFF) is a no-op in NULL mode.
// This ordering keeps Arduino and the monitor driver synchronized.
namespace PassiveWifiLifecycle {
template<class Radio>
bool start(Radio& radio) {
  if (!radio.arduinoStation()) return false;
  radio.disconnectStation(); // No network credentials are erased.
  radio.cancelScan();
  radio.disableMonitor();
  if (!radio.nullMode()) return false;
  // Explicit start also repairs a stale Arduino started cache from a prior tool.
  return radio.startDriver();
}

template<class Radio>
bool stop(Radio& radio) {
  radio.disableMonitor();
  radio.detachReceiver();
  if (!radio.initialized()) return true;
  // A non-NULL mode makes Arduino's OFF path actually stop/deinitialize the
  // driver and clear its private cached started flag.
  if (!radio.stationMode()) return false;
  return radio.arduinoOff();
}
}
