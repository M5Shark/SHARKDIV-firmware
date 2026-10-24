#pragma once
// Optional receive-only RF context. No measurement establishes camera identity.
namespace flock_sensors {
void begin();
int sampleNrf();            // 0-100 sampled channel occupancy; -1 no module
int sampleLte();            // historical name: sub-GHz RSSI index; -1 no module
bool sampleIr(int* adcOut); // false without a raw photodiode
bool lteBurstSeen();        // historical name: threshold exceeded at a spot
void poll();
}
