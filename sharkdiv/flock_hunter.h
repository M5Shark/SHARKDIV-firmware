#pragma once
// flock_hunter.h — passive Flock Safety camera detector for ESP32-DIV.
//
// Ported from colonelpanichacks/flock-you (MIT). PASSIVE ONLY: listens in
// WiFi promiscuous mode for the Flock camera probe-request signature and
// lists hits on screen. Does NOT transmit, authenticate, or start an AP.
//
// Research credit preserved: OUI set + DeFlock Joplin IE fingerprint from the
// flock-you project and the DeFlock research it cites.
//
// Candidate gates (the fingerprint and OUI paths remain distinguishable):
//   Valid wildcard probe + (published transmitter OUI OR full IE fingerprint).
//   Each row distinguishes OUI evidence from an exact fingerprint; neither
//   establishes the device identity or operator. Locally administered matches
//   are retained, including the published 82:6b:f2 prefix.
//
// Integration surface (defined in flock_hunter.cpp). The sketch includes this
// header and calls, mirroring every other WiFi feature:
//   FlockHunter::flockSetup();   // once, on entry
//   FlockHunter::flockLoop();    // repeatedly, from the feature while-loop
//   FlockHunter::flockStop();    // once, on exit (stops promiscuous mode)

#include <stdint.h>
#include "FlockSignatures.h"
#include <string.h>

namespace FlockHunter {

// ── feature entry points (defined in flock_hunter.cpp) ──
void flockSetup();
void flockLoop();
void flockStop();

// Published candidate data is shared with the suite and Wi-Fi camera scanner.
static constexpr int kRssiMin = -95;
static constexpr uint8_t kChannels[] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
static constexpr uint32_t kChannelDwellMs = 250;
static constexpr uint32_t kHitTtlMs = 120000;
static constexpr uint8_t kMaxDetections = 64;

// ── 802.11 MAC header (packed) ──
typedef struct {
  uint16_t frame_ctrl;
  uint16_t duration;
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  uint16_t seq_ctrl;
} __attribute__((packed)) MacHdr;

// ── one observed candidate address ──
struct Detection {
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  channel;
  uint16_t count;
  bool     fingerprint;
  bool     byOui;
  uint32_t firstSeen;
  uint32_t lastSeen;
};

// Compatibility helper for the standalone feature. ESP32 callback bodies carry
// a four-byte FCS; matching requires a fully valid IE stream after removing it.
static inline bool matchOui(const uint8_t* mac) { return FlockSignatures::wifiOui(mac); }
static inline bool isFlockProbe(const MacHdr* hdr, const uint8_t* body, int bodyLen) {
  if (!hdr || !body || bodyLen < 4 || (hdr->frame_ctrl & 0x03ff) != 0x0040) return false;
  const auto evidence = FlockSignatures::probeBody(body, size_t(bodyLen - 4));
  return evidence.valid && evidence.wildcard && (matchOui(hdr->addr2) || evidence.fingerprint);
}
}  // namespace FlockHunter
