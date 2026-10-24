#pragma once
#include <stddef.h>
#include <stdint.h>

namespace DetectionAlerts {
enum class Source : uint8_t {
  WiFi, BLE, FindMy, Drone, Flock, Camera, Gear, RF24, SubGhz,
  Infrared, RFID, GPS, Count
};

// No allocation: bounded work even in a busy radio receive callback.
// A continuing observation stays quiet. A device seen again after 30 seconds
// of absence can alert again. One chirp covers a burst of simultaneous results.
class Policy {
 public:
  static constexpr uint32_t repeatMs = 30000;
  static constexpr uint32_t burstMs = 1000;
  bool offer(Source source, uint32_t identity, uint32_t now) {
    if (source >= Source::Count) return false;
    size_t slot = 0;
    uint32_t oldest = 0;
    bool matched = false;
    for (size_t i = 0; i < capacity; ++i) {
      Entry& e = entries[i];
      if (e.used && e.source == source && e.identity == identity) {
        const bool recent = uint32_t(now - e.seen) < repeatMs;
        e.seen = now;
        if (recent) return false;
        slot = i;
        matched = true;
        break;
      }
      const uint32_t age = e.used ? uint32_t(now - e.seen) : UINT32_MAX;
      if (i == 0 || age > oldest) { oldest = age; slot = i; }
    }
    // Do not churn a full live set: cycling 65 devices through a 64-entry LRU
    // otherwise makes old devices look new forever. Coalesce overflow until a
    // slot expires. Receiver/display tables are independent and keep working.
    if (!matched && entries[slot].used && oldest < repeatMs) return false;
    entries[slot] = {identity, now, source, true};
    if (hasAlert && uint32_t(now - lastAlert) < burstMs) return false;
    hasAlert = true;
    lastAlert = now;
    return true;
  }
  void reset() { *this = Policy(); }
 private:
  static constexpr size_t capacity = 64;
  struct Entry { uint32_t identity; uint32_t seen; Source source; bool used; };
  Entry entries[capacity] = {};
  uint32_t lastAlert = 0;
  bool hasAlert = false;
};
}
