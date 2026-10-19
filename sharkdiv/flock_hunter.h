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
// Detection gates (all must pass), matching flock-you's enabled path:
//   1. addr2 OUI in the Flock prefix table (locally-administered MACs rejected)
//   2. management frame, type 0 subtype 4 (probe request)
//   3. first SSID IE (tag 0) has length 0 (wildcard probe)
//   4. IE signature == the primary DeFlock fingerprint
//
// Integration surface (defined in flock_hunter.cpp). The sketch includes this
// header and calls, mirroring every other WiFi feature:
//   FlockHunter::flockSetup();   // once, on entry
//   FlockHunter::flockLoop();    // repeatedly, from the feature while-loop
//   FlockHunter::flockStop();    // once, on exit (stops promiscuous mode)

#include <Arduino.h>
#include <string.h>

namespace FlockHunter {

// ── feature entry points (defined in flock_hunter.cpp) ──
void flockSetup();
void flockLoop();
void flockStop();

// ── stable, spec-fixed detection constants (do not change blindly) ──
static const uint8_t kFlockOui[][3] = {
  {0x70,0xc9,0x4e},{0x3c,0x91,0x80},{0xd8,0xf3,0xbc},{0x80,0x30,0x49},
  {0xb8,0x35,0x32},{0x14,0x5a,0xfc},{0x74,0x4c,0xa1},{0x08,0x3a,0x88},
  {0x9c,0x2f,0x9d},{0xc0,0x35,0x32},{0x94,0x08,0x53},{0xe4,0xaa,0xea},
  {0xf4,0x6a,0xdd},{0xf8,0xa2,0xd6},{0x24,0xb2,0xb9},{0x00,0xf4,0x8d},
  {0xd0,0x39,0x57},{0xe8,0xd0,0xfc},{0xe0,0x4f,0x43},{0xb8,0x1e,0xa4},
  {0x70,0x08,0x94},{0x58,0x8e,0x81},{0xec,0x1b,0xbd},{0x3c,0x71,0xbf},
  {0x58,0x00,0xe3},{0x90,0x35,0xea},{0x5c,0x93,0xa2},{0x64,0x6e,0x69},
  {0x48,0x27,0xea},{0xa4,0xcf,0x12},{0x82,0x6b,0xf2},
};
static const uint8_t kFlockOuiCount = sizeof(kFlockOui) / 3;

// Primary DeFlock Joplin IE fingerprint (exact string the builder must equal).
static const char kFlockIeSigPrimary[] =
  "2,12,127,221:506f9a16030103,45,191,221:0050f208000000";

static const int   kRssiMin        = -95;     // drop weaker frames
static const uint32_t kDedupeMs     = 5000;    // per-MAC output cooldown
static const uint8_t  kChannels[]   = {11, 6, 1};
static const uint32_t kChannelDwellMs = 250;
static const uint8_t  kMaxDetections = 64;     // on-device table cap (ESP32-DIV)

// ── 802.11 MAC header (packed) ──
typedef struct {
  uint16_t frame_ctrl;
  uint16_t duration;
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  uint16_t seq_ctrl;
} __attribute__((packed)) MacHdr;

// ── one tracked camera ──
struct Detection {
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  channel;
  uint16_t count;
  uint32_t firstSeen;
  uint32_t lastSeen;
};

// ── stable matchers (header-inline so they can be unit-reasoned) ──

// Reject locally-administered, then compare the 3-byte prefix.
static inline bool matchOui(const uint8_t* mac) {
  if (mac[0] & 0x02) return false;
  for (uint8_t i = 0; i < kFlockOuiCount; i++) {
    if (mac[0] == kFlockOui[i][0] && mac[1] == kFlockOui[i][1] &&
        mac[2] == kFlockOui[i][2]) return true;
  }
  return false;
}

// First SSID IE (tag 0): 1 = wildcard (len 0), 0 = directed, -1 = not found.
static inline int wildcardProbeIe(const uint8_t* body, int len) {
  int i = 0;
  while (i + 2 <= len) {
    uint8_t tag = body[i];
    uint8_t l   = body[i + 1];
    if (i + 2 + l > len) return -1;      // malformed / truncated
    if (tag == 0) return (l == 0) ? 1 : 0;
    i += 2 + l;
  }
  return -1;
}

// Build the IE token signature (SSID skipped; vendor IE 221 -> "221:"+<=8 hex
// payload bytes; other tags -> decimal), then compare to the primary string.
// Faithful to flock-you's builder for the enabled single-signature path.
static inline bool flockIeSigMatch(const uint8_t* body, int len) {
  char sig[96];
  int  si = 0;
  int  i  = 0;
  bool first = true;
  const char* hex = "0123456789abcdef";
  while (i + 2 <= len) {
    uint8_t tag = body[i];
    uint8_t l   = body[i + 1];
    if (i + 2 + l > len) break;
    if (tag != 0) {                      // skip SSID
      if (!first && si < (int)sizeof(sig) - 1) sig[si++] = ',';
      first = false;
      if (tag == 221) {                  // vendor: 221:<=8 payload hex bytes
        const char* p = "221:";
        while (*p && si < (int)sizeof(sig) - 1) sig[si++] = *p++;
        uint8_t n = l < 8 ? l : 8;
        for (uint8_t b = 0; b < n && si < (int)sizeof(sig) - 2; b++) {
          uint8_t v = body[i + 2 + b];
          sig[si++] = hex[v >> 4];
          sig[si++] = hex[v & 0x0f];
        }
      } else {                           // decimal tag number
        char num[4]; int ni = 0; int t = tag;
        if (t == 0) num[ni++] = '0';
        while (t > 0) { num[ni++] = '0' + (t % 10); t /= 10; }
        while (ni > 0 && si < (int)sizeof(sig) - 1) sig[si++] = num[--ni];
      }
    }
    i += 2 + l;
  }
  sig[si < (int)sizeof(sig) ? si : (int)sizeof(sig) - 1] = '\0';
  return strcmp(sig, kFlockIeSigPrimary) == 0;
}

// Full gate: returns true if this promiscuous payload is a Flock probe.
// body/bodyLen = frame payload AFTER the 24-byte MAC header.
static inline bool isFlockProbe(const MacHdr* hdr, const uint8_t* body, int bodyLen) {
  if (!matchOui(hdr->addr2)) return false;
  uint8_t ftype   = (hdr->frame_ctrl >> 2) & 0x03;
  uint8_t subtype = (hdr->frame_ctrl >> 4) & 0x0f;
  if (ftype != 0 || subtype != 4) return false;           // probe request
  int w = wildcardProbeIe(body, bodyLen);
  if (w < 0 && bodyLen >= 4) w = wildcardProbeIe(body, bodyLen - 4); // strip FCS
  if (w != 1) return false;
  if (flockIeSigMatch(body, bodyLen)) return true;
  if (bodyLen >= 4 && flockIeSigMatch(body, bodyLen - 4)) return true;
  return false;
}

}  // namespace FlockHunter
