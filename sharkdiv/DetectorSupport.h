#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Portable receive parsing and bounded records shared by the detector UI.
// Radio callbacks own no display or filesystem work.
namespace DetectorSupport {
enum Evidence : uint8_t { Name = 1, Vendor = 2, Service = 4, Probe = 8, Advertisement = 16, Fingerprint = 32 };

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool parseOui(const char* text, uint8_t out[3]) {
  if (!text || strlen(text) != 8 || text[2] != ':' || text[5] != ':') return false;
  uint8_t parsed[3];
  for (int i = 0; i < 3; ++i) {
    const int high = hexDigit(text[3 * i]), low = hexDigit(text[3 * i + 1]);
    if (high < 0 || low < 0) return false;
    parsed[i] = static_cast<uint8_t>(high * 16 + low);
  }
  if (parsed[0] & 1) return false; // Multicast prefixes cannot identify a transmitter.
  memcpy(out, parsed, 3);
  return true;
}

struct OuiList {
  uint8_t values[24][3] = {};
  uint8_t count = 0;
  bool add(const char* text) {
    uint8_t oui[3];
    if (!parseOui(text, oui)) return false;
    for (uint8_t i = 0; i < count; ++i)
      if (!memcmp(values[i], oui, 3)) return true;
    if (count == 24) return false;
    memcpy(values[count++], oui, 3);
    return true;
  }
  bool matches(const uint8_t* mac) const {
    if (!mac || (mac[0] & 1)) return false;
    for (uint8_t i = 0; i < count; ++i)
      if (!memcmp(values[i], mac, 3)) return true;
    return false;
  }
};

struct WifiAdvertisement {
  uint8_t mac[6] = {};
  char ssid[33] = {};
  bool probe = false;
  bool wildcard = false;
};

// Input excludes FCS. Only beacon, probe response and probe request management
// frames have the fields inspected here. Every IE is bounds checked.
inline bool parseWifiAdvertisement(const uint8_t* frame, size_t length,
                                   WifiAdvertisement& out) {
  if (!frame || length < 24 || length > 2352 || (frame[0] & 15) ||
      (frame[1] & 0x40) || (frame[10] & 1)) return false;
  const uint8_t subtype = frame[0] >> 4;
  if (subtype != 4 && subtype != 5 && subtype != 8) return false;
  const size_t start = subtype == 4 ? 24 : 36;
  if (length < start) return false;
  WifiAdvertisement candidate;
  candidate.probe = subtype == 4;
  memcpy(candidate.mac, frame + 10, 6);
  bool hasSsid = false;
  for (size_t offset = start; offset < length;) {
    if (length - offset < 2) return false;
    const uint8_t tag = frame[offset++];
    const size_t bytes = frame[offset++];
    if (bytes > length - offset) return false;
    if (tag == 0) {
      if (hasSsid || bytes > 32) return false;
      hasSsid = true;
      candidate.wildcard = candidate.probe && bytes == 0;
      for (size_t i = 0; i < bytes; ++i) {
        const uint8_t c = frame[offset + i];
        candidate.ssid[i] = c >= 32 && c <= 126 ? c : '?';
      }
    }
    offset += bytes;
  }
  if (!hasSsid) return false;
  out = candidate;
  return true;
}

struct Hit {
  char desc[40] = {};
  char mac[18] = {};
  int8_t rssi = -127;
  uint8_t channel = 0; // Zero BLE; otherwise Wi-Fi channel.
  uint8_t evidence = 0;
  uint32_t lastMs = 0, firstMs = 0;
  uint16_t seen = 0;
};
class Table {
public:
  static constexpr int capacity = 14;
  Hit entries[capacity] = {};
  int count = 0;
  void clear() { count = 0; }
  void prune(uint32_t now) {
    int kept = 0;
    for (int i = 0; i < count; ++i)
      if (now - entries[i].lastMs <= 30000) entries[kept++] = entries[i];
    count = kept;
  }
  void observe(const char* label, const char* mac, int8_t rssi, uint8_t channel,
               uint8_t evidence, uint32_t now) {
    prune(now);
    int slot = -1;
    for (int i = 0; i < count; ++i)
      if (!strcmp(entries[i].mac, mac) &&
          (entries[i].channel == 0) == (channel == 0)) { slot = i; break; }
    if (slot < 0) {
      if (count < capacity) slot = count++;
      else {
        slot = 0;
        for (int i = 1; i < count; ++i)
          if (now - entries[i].lastMs > now - entries[slot].lastMs) slot = i;
      }
      entries[slot] = Hit{};
      strncpy(entries[slot].mac, mac, sizeof(entries[slot].mac) - 1);
      entries[slot].firstMs = now;
    }
    Hit& hit = entries[slot];
    if (label && *label) {
      strncpy(hit.desc, label, sizeof(hit.desc) - 1);
      hit.desc[sizeof(hit.desc) - 1] = 0;
    }
    hit.rssi = rssi;
    hit.channel = channel;
    hit.evidence |= evidence;
    hit.lastMs = now;
    if (hit.seen < 65535) ++hit.seen;
  }
  int snapshot(Hit* out, int maximum, uint32_t now) {
    prune(now);
    const int amount = count < maximum ? count : maximum;
    if (amount > 0) memcpy(out, entries, static_cast<size_t>(amount) * sizeof(Hit));
    return amount;
  }
};
}
