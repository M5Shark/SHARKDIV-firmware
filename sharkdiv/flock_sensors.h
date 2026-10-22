#pragma once
// flock_sensors.h — supporting evidence, strictly RX: 2.4G channel activity
// (nRF24 carrier detect), LTE-band RSSI bursts (CC1101), IR check.
namespace flock_sensors {
void begin();
// Quick sample pass; each returns its latest metric.
int  sampleNrf();            // 0-100 activity index (RPD hits)
int  sampleLte();            // 0-100 burst strength (max RSSI over spots)
bool sampleIr(int* adcOut);  // false = no photodiode fitted (TSOP only)
bool lteBurstSeen();
void poll();
}  // namespace flock_sensors
