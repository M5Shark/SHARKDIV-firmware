#pragma once

#include <stdint.h>

namespace RfOccupancy {
// RPD is one threshold bit, not a power measurement. Percent means the fraction
// of sampled dwell windows above the receiver's threshold (about -64 dBm).
inline uint8_t percent(uint16_t hits, uint16_t samples) {
  if (samples == 0) return 0;
  if (hits >= samples) return 100;
  return static_cast<uint8_t>((uint32_t(hits) * 100u) / samples);
}

inline uint8_t aboveBaseline(uint8_t measured, uint8_t baseline) {
  return measured > baseline ? uint8_t(measured - baseline) : 0;
}
}  // namespace RfOccupancy
