#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Original, receive-only helpers. A matching broadcast is a Find My candidate,
// never proof of an AirTag, a unique device, its owner, or unwanted tracking.
namespace FindMyScanner {

inline bool parseManufacturerData(const uint8_t* data, size_t size, uint8_t& status) {
  // Company ID (little endian), type, declared payload length, 25-byte payload.
  // Reject truncated, overlong, and proximity-pairing advertisements.
  if (!data || size != 29 || data[0] != 0x4c || data[1] != 0x00 ||
      data[2] != 0x12 || data[3] != 0x19) {
    return false;
  }
  status = data[4];
  return true;
}

struct Candidate {
  uint8_t mac[6];
  int8_t rssi;
  uint8_t status;
  uint16_t hits;
  uint32_t lastSeen;
};

template <size_t Capacity>
class CandidateTable {
 public:
  static_assert(Capacity > 0, "Candidate storage must not be empty");
  Candidate records[Capacity] = {};
  size_t count = 0;

  void clear() { count = 0; }

  bool expire(uint32_t now, uint32_t maxAgeMs) {
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
      if (uint32_t(now - records[i].lastSeen) < maxAgeMs) {
        records[kept++] = records[i];
      }
    }
    const bool changed = kept != count;
    count = kept;
    return changed;
  }

  void observe(const uint8_t mac[6], int8_t rssi, uint8_t status, uint32_t now) {
    size_t index = count;
    for (size_t i = 0; i < count; ++i) {
      if (memcmp(records[i].mac, mac, 6) == 0) {
        index = i;
        break;
      }
    }
    if (index == count) {
      if (count < Capacity) {
        ++count;
      } else {
        // Recent observations replace the oldest, regardless of signal strength.
        index = 0;
        for (size_t i = 1; i < count; ++i) {
          if (uint32_t(now - records[i].lastSeen) >
              uint32_t(now - records[index].lastSeen)) {
            index = i;
          }
        }
      }
      records[index] = {};
      memcpy(records[index].mac, mac, 6);
    }
    Candidate& hit = records[index];
    hit.rssi = rssi;
    hit.status = status;
    if (hit.hits < UINT16_MAX) ++hit.hits;
    hit.lastSeen = now;
  }
};

}  // namespace FindMyScanner
