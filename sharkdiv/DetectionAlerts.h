#pragma once
#include "DetectionAlertPolicy.h"

namespace DetectionAlerts {
// Call begin once after settings load. Only this service owns the buzzer PWM.
void begin(bool enabled);
void setEnabled(bool enabled);
bool enabled();
bool available();
// Safe from radio task callbacks, not from hardware ISRs. Never delays a scan.
// Ordinary detections are silent outside a foreground tool.
void notify(Source source, uint32_t identity = 0);
uint32_t hash(const uint8_t* data, size_t length);
bool preview();
void printStatus();
}
