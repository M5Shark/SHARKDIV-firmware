#pragma once

// flock_config.h — every pin, tuning value and signature for the Flock
// passive detector. Pins reference the board macros from shared.h so there
// is exactly ONE place to change hardware.

#include "shared.h"

// ── radios / peripherals (from shared.h — do not redefine here) ──
// WiFi + BLE      : the ESP32's own radios (coex handled by the SDK;
//                   we time-slice scans to keep both stable)
// nRF24L01        : NRF24_SCAN_CE / NRF24_SCAN_CSN / NRF24_SPI_*
// CC1101          : CC1101_CS / CC1101_GDO0 / CC1101_SPI pins
// GPS             : GPS_UART_RX / GPS_UART_TX / GPS_UART_BAUD
// Buzzer          : BUZZER_PIN (DetectionAlerts service, LEDC channel 2)
// SD              : SD_CS via the firmware's sdSpiInit()/sdMountChipSelect()

namespace flk {

// ── tunables ──
constexpr uint32_t kWifiWindowMs   = 4000;  // async WiFi scan window
constexpr uint32_t kProbeWindowMs  = 3000;  // promiscuous probe window
constexpr uint32_t kBleWindowMs    = 4000;  // passive BLE window
constexpr uint32_t kSensorTickMs   = 1200;  // nRF24/CC1101 sample period
constexpr int      kRssiPossible   = -95;   // weak matched broadcasts still count
constexpr int      kNrfActiveHits  = 8;     // RPD hits/channels => activity
constexpr int      kLteRssiDbm     = -70;   // CC1101 burst threshold
constexpr uint32_t kRealertMs      = 30000; // same-MAC re-alert suppression
constexpr uint32_t kHitTtlMs       = 120000;// drop devices unseen this long
constexpr float    kFollowMinM     = 500.0f;// follow-alert distance
constexpr int      kFollowFixes    = 3;     // positions needed for follow

// CC1101 sub-GHz spot frequencies. RSSI cannot identify cellular traffic.
constexpr float kLteMHz[3] = { 850.0f, 868.0f, 895.0f };

// ── signatures ──
// Legacy signature arrays; live receivers use validated FlockSignatures.h.
constexpr const char* kSsidPatterns[] = {
  "Flock-", "Flock Camera", "penguin", "pigvision"
};
constexpr int kSsidPatternCount = 4;

// Known Flock OUI prefixes (first 3 MAC bytes), extendable from SD
// /flock/ouis.txt (one xx:xx:xx per line).
constexpr const char* kOuis[] = {
  "" // no placeholder vendor prefixes; see FlockSignatures.h and user overrides
};
constexpr int kOuiCount = 0;

// BLE advertisement name fragments (Flock / Raven devices).
constexpr const char* kBleNames[] = { "Flock", "flock", "Raven", "raven" };
constexpr int kBleNameCount = 4;

// Session evidence index; ambient RF must not establish device identity.
constexpr int kScoreWifi = 35;
constexpr int kScoreBle  = 25;
constexpr int kScoreLte  = 0;
constexpr int kScoreNrf  = 0;
constexpr int kScoreIr   = 0;

}  // namespace flock
