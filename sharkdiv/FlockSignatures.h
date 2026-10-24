#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Original bounded parsers using published signature data from flock-you:
// https://github.com/colonelpanichacks/flock-you (MIT), main, checked 2026-10-24.
// Community OUI research: NitekryDPaul / DeFlockJoplin; firmware-derived data:
// datasets/firmware_derived_signatures.md. Every match is evidence, not proof
// of a camera or its operator; vendor prefixes and service IDs may be shared.
namespace FlockSignatures {

static constexpr uint8_t kWifiOuis[][3] = {
  {0x70,0xc9,0x4e},{0x3c,0x91,0x80},{0xd8,0xf3,0xbc},{0x80,0x30,0x49},
  {0xb8,0x35,0x32},{0x14,0x5a,0xfc},{0x74,0x4c,0xa1},{0x08,0x3a,0x88},
  {0x9c,0x2f,0x9d},{0xc0,0x35,0x32},{0x94,0x08,0x53},{0xe4,0xaa,0xea},
  {0xf4,0x6a,0xdd},{0xe0,0x0a,0xf6},{0x24,0xb2,0xb9},{0x00,0xf4,0x8d},
  {0xd0,0x39,0x57},{0xe8,0xd0,0xfc},{0xe0,0x4f,0x43},{0xb8,0x1e,0xa4},
  {0x70,0x08,0x94},{0x58,0x8e,0x81},{0xec,0x1b,0xbd},{0x3c,0x71,0xbf},
  {0x58,0x00,0xe3},{0x90,0x35,0xea},{0x5c,0x93,0xa2},{0x64,0x6e,0x69},
  {0x48,0x27,0xea},{0xa4,0xcf,0x12},{0x14,0xb5,0xcd},{0x82,0x6b,0xf2},
  {0xb4,0x1e,0x52},{0x00,0x03,0x7f}
};
static constexpr size_t kWifiOuiCount = sizeof(kWifiOuis) / sizeof(kWifiOuis[0]);

inline bool wifiOui(const uint8_t* mac) {
  if (!mac || (mac[0] & 1)) return false;
  // 82:6b:f2 is a published signature with its locally-administered bit set.
  for (size_t i = 0; i < kWifiOuiCount; ++i) {
    if (memcmp(mac, kWifiOuis[i], 3) == 0) return true;
  }
  return false;
}

inline uint8_t lower(uint8_t c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
inline bool equalText(const uint8_t* text, size_t len, const char* expected) {
  if (!text || len != strlen(expected)) return false;
  for (size_t i = 0; i < len; ++i) if (lower(text[i]) != lower(expected[i])) return false;
  return true;
}
inline bool decimalDigits(const uint8_t* text, size_t len) {
  if (!text || len == 0) return false;
  for (size_t i = 0; i < len; ++i) if (text[i] < '0' || text[i] > '9') return false;
  return true;
}
inline bool namedFamily(const uint8_t* text, size_t len, const char* family) {
  const size_t prefix = strlen(family);
  if (len < prefix || !equalText(text, prefix, family)) return false;
  return len == prefix || text[prefix] == '-' || text[prefix] == ' ';
}
inline bool ssid(const char* name) {
  if (!name) return false;
  const size_t len = strlen(name);
  const uint8_t* text = reinterpret_cast<const uint8_t*>(name);
  if (equalText(text, len, "flock")) return true;
  if (len != 12 || !equalText(text, 6, "flock-")) return false;
  for (size_t i = 6; i < len; ++i) {
    const uint8_t c = lower(text[i]);
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

struct ProbeEvidence {
  bool valid = false;
  bool wildcard = false;
  bool fingerprint = false;
  bool oui = false;
};

inline ProbeEvidence probeBody(const uint8_t* body, size_t length) {
  ProbeEvidence result;
  if (!body) return result;
  static const uint8_t tags[] = {2,12,127,221,45,191,221};
  static const uint8_t vendor1[] = {0x50,0x6f,0x9a,0x16,0x03,0x01,0x03};
  static const uint8_t vendor2[] = {0x00,0x50,0xf2,0x08,0x00,0x00,0x00};
  bool seenSsid = false;
  bool signature = true;
  size_t token = 0;
  size_t offset = 0;
  while (offset < length) {
    if (length - offset < 2) return ProbeEvidence{};
    const uint8_t tag = body[offset++];
    const size_t size = body[offset++];
    if (size > length - offset) return ProbeEvidence{};
    if (tag == 0) {
      if (seenSsid || size > 32) return ProbeEvidence{};
      seenSsid = true;
      result.wildcard = size == 0;
    } else {
      if (token >= sizeof(tags) || tag != tags[token]) signature = false;
      if (tag == 221 && token < sizeof(tags)) {
        const uint8_t* vendor = token == 3 ? vendor1 : vendor2;
        if (size != sizeof(vendor1) || memcmp(body + offset, vendor, sizeof(vendor1)) != 0)
          signature = false;
      }
      ++token;
    }
    offset += size;
  }
  result.valid = seenSsid;
  result.fingerprint = seenSsid && result.wildcard && signature && token == sizeof(tags);
  return result;
}

// ESP32 promiscuous rx_ctrl.sig_len includes four FCS bytes; callers using
// captures with no FCS must explicitly pass false rather than guessing.
inline ProbeEvidence probeFrame(const uint8_t* frame, size_t length, bool includesFcs = true) {
  if (!frame || length < 24 + (includesFcs ? 4u : 0u)) return ProbeEvidence{};
  if (includesFcs) length -= 4;
  if (frame[0] != 0x40 || (frame[1] & 0x03) != 0) return ProbeEvidence{};
  uint8_t sourceBits = 0;
  for (size_t i = 10; i < 16; ++i) sourceBits |= frame[i];
  if (!sourceBits || (frame[10] & 1)) return ProbeEvidence{};
  ProbeEvidence result = probeBody(frame + 24, length - 24);
  result.oui = wifiOui(frame + 10);
  return result;
}

enum BleEvidence : uint8_t { ByName = 1, ByManufacturer = 2, ByService = 4 };

inline uint8_t bleAdvertisementEvidence(const uint8_t* data, size_t length) {
  if (!data) return 0;
  uint8_t evidence = 0;
  bool genericName = false;
  static const uint8_t accessoryUuidLE[16] = {
    0x6f,0x2e,0x17,0xdf,0x14,0x18,0xe5,0x9f,0xa8,0x46,0x32,0x95,0x38,0xbb,0xcc,0xe8
  };
  for (size_t offset = 0; offset < length;) {
    const size_t adLength = data[offset++];
    if (adLength == 0) break;
    if (adLength > length - offset) return 0;
    const uint8_t type = data[offset];
    const uint8_t* value = data + offset + 1;
    const size_t size = adLength - 1;
    if (type == 0x08 || type == 0x09) {
      if (namedFamily(value, size, "flock") || namedFamily(value, size, "raven") ||
          equalText(value, size, "fs ext battery") ||
          (size == 18 && equalText(value, 8, "penguin-") && decimalDigits(value + 8, 10))) {
        evidence |= ByName;
      }
      genericName |= (size == 10 && decimalDigits(value, size)) || equalText(value, size, "dfutarg");
    } else if (type == 0xff && size >= 2 && value[0] == 0xc8 && value[1] == 0x09) {
      evidence |= ByManufacturer;
    } else if (type == 0x02 || type == 0x03 || type == 0x16) {
      if (size < 2 || (type != 0x16 && size % 2)) return 0;
      const size_t serviceBytes = type == 0x16 ? 2 : size;
      for (size_t i = 0; i < serviceBytes; i += 2) {
        if (value[i] == 0 && value[i+1] >= 0x31 && value[i+1] <= 0x35) evidence |= ByService;
      }
    } else if (type == 0x06 || type == 0x07 || type == 0x21) {
      if (size < 16 || (type != 0x21 && size % 16)) return 0;
      const size_t serviceBytes = type == 0x21 ? 16 : size;
      for (size_t i = 0; i < serviceBytes; i += 16)
        if (memcmp(value + i, accessoryUuidLE, 16) == 0) evidence |= ByService;
    }
    offset += adLength;
  }
  if (genericName && (evidence & ByManufacturer)) evidence |= ByName;
  return evidence;
}
}  // namespace FlockSignatures
