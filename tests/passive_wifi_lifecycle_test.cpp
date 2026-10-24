#include "../sharkdiv/PassiveWifiLifecycle.h"
#include <assert.h>
#include <stdio.h>

// Model the relevant Arduino-ESP32 2.0.10 WiFiGeneric.cpp behavior: getMode()
// is gated by its private cachedStarted flag, mode(same) returns immediately,
// espWiFiStart() skips the IDF start when cachedStarted is already true, and
// mode(OFF) cannot enter its stop path while the raw mode is NULL.
struct CachedArduinoRadio {
  bool init = false, driverStarted = false, cachedStarted = false;
  bool monitor = false, receiverAttached = false, connected = false;
  bool failStation = false, failNull = false, failDriverStart = false;
  int mode = 0, cancellations = 0, credentialErases = 0;
  int arduinoMode() const { return init && cachedStarted ? mode : 0; }
  bool arduinoStation() {
    if (failStation) return false;
    if (arduinoMode() == 1) return true;
    init = true;
    mode = 1;
    if (cachedStarted) return true;
    cachedStarted = true;
    driverStarted = true;
    return true;
  }
  void disconnectStation() { connected = false; }
  void cancelScan() { ++cancellations; }
  void disableMonitor() { monitor = false; }
  bool nullMode() { if (failNull) return false; mode = 0; return true; }
  bool startDriver() {
    if (!init || failDriverStart) return false;
    driverStarted = true;
    return true;
  }
  void detachReceiver() { receiverAttached = false; }
  bool initialized() { return init; }
  bool stationMode() { if (failStation) return false; mode = 1; return true; }
  bool arduinoOff() {
    if (arduinoMode() == 0) return true;
    cachedStarted = false;
    driverStarted = false;
    init = false;
    return true;
  }
  bool flockPassiveScan() { return arduinoStation() && init && driverStarted; }
};

int main() {
  CachedArduinoRadio legacy;
  assert(PassiveWifiLifecycle::start(legacy));
  // The former raw-stop implementation leaves Arduino thinking it is running.
  legacy.driverStarted = false;
  assert(legacy.cachedStarted);
  assert(!legacy.flockPassiveScan());
  // Explicit monitor start repairs even an already stale cache from another tool.
  assert(PassiveWifiLifecycle::start(legacy));
  assert(legacy.driverStarted && legacy.cachedStarted && legacy.mode == 0);
  assert(PassiveWifiLifecycle::stop(legacy));
  assert(!legacy.cachedStarted && !legacy.driverStarted && !legacy.init);
  assert(legacy.flockPassiveScan());

  CachedArduinoRadio radio;
  for (int launch = 0; launch < 8; ++launch) {
    assert(PassiveWifiLifecycle::start(radio));
    radio.monitor = true;
    radio.receiverAttached = true;
    assert(radio.driverStarted && radio.mode == 0 && radio.cachedStarted);
    assert(PassiveWifiLifecycle::stop(radio));
    assert(!radio.driverStarted && !radio.cachedStarted && !radio.init);
    assert(!radio.monitor && !radio.receiverAttached && radio.credentialErases == 0);
    assert(radio.flockPassiveScan());
    assert(radio.arduinoOff());
  }
  radio.failStation = true;
  assert(!PassiveWifiLifecycle::start(radio));
  radio.failStation = false;
  radio.failNull = true;
  assert(!PassiveWifiLifecycle::start(radio));
  assert(PassiveWifiLifecycle::stop(radio));
  radio.failNull = false;
  radio.failDriverStart = true;
  assert(!PassiveWifiLifecycle::start(radio));
  assert(PassiveWifiLifecycle::stop(radio));
  radio.failDriverStart = false;
  assert(PassiveWifiLifecycle::start(radio));
  radio.failStation = true;
  assert(!PassiveWifiLifecycle::stop(radio));
  puts("Passive WiFi lifecycle: stale cache regression, repeated launches and next Flock scan passed");
}
