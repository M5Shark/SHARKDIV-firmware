#include "RemoteIdParser.h"
#include <string.h>

namespace RemoteId {
namespace {
// Original byte-oriented implementation of the documented ASTM Basic ID and
// Location wire fields. No upstream C library is linked into the firmware.
// Reference layout: opendroneid-core-c public protocol structs; the host tests
// separately compare fixtures with the Apache-licensed upstream implementation.
constexpr size_t kMessageBytes = 25;
constexpr size_t kIdBytes = 20;
constexpr uint8_t kLastProtocolVersion = 2;
constexpr uint8_t kMaxPackedMessages = 9;

uint16_t readLe16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

double coordinate(const uint8_t* bytes) {
  uint32_t word = 0;
  for (unsigned i = 0; i < 4; ++i) word |= static_cast<uint32_t>(bytes[i]) << (8 * i);
  const int64_t signedWord = word & 0x80000000u
      ? static_cast<int64_t>(word) - 4294967296LL : static_cast<int64_t>(word);
  return static_cast<double>(signedWord) / 10000000.0;
}

bool decodeSingle(const uint8_t* data, Observation& out) {
  if ((data[0] & 15) > kLastProtocolVersion) return false;
  const uint8_t type = data[0] >> 4;
  if (type > 5) return false;
  out.version = data[0] & 15;
  if (type == 0) {
    const uint8_t idType = data[1] >> 4;
    if (idType < 1 || idType > 4) return false;
    out.idType = idType;
    out.aircraftType = data[1] & 15;
    memset(out.id, 0, sizeof(out.id));
    if (idType == 1 || idType == 2) { // Serial number or CAA registration.
      bool nonempty = false;
      for (size_t i = 0; i < kIdBytes && data[2 + i]; ++i) {
        const uint8_t c = data[2 + i];
        out.id[i] = c >= 32 && c <= 126 ? static_cast<char>(c) : '?';
        nonempty = true;
      }
      if (!nonempty) return false;
    } else { // UTM UUID and specific session IDs are binary, not C strings.
      const char* hex = "0123456789ABCDEF";
      for (size_t i = 0; i < kIdBytes; ++i) {
        out.id[i * 2] = hex[data[2 + i] >> 4];
        out.id[i * 2 + 1] = hex[data[2 + i] & 15];
      }
    }
    out.hasId = true;
  } else if (type == 1) {
    const uint8_t status = data[1] >> 4;
    const double latitude = coordinate(data + 5);
    const double longitude = coordinate(data + 9);
    if (status > 4 || latitude < -90 || latitude > 90 ||
        longitude < -180 || longitude > 180) return false;
    out.hasLocation = true;
    out.status = status;
    out.latitude = latitude;
    out.longitude = longitude;
    out.positionValid = latitude != 0 || longitude != 0;
    const uint16_t altitudeCode = readLe16(data + 15);
    out.altitude = static_cast<float>(altitudeCode) * 0.5f - 1000.0f;
    out.altitudeValid = altitudeCode != 0;
    const bool highSpeedRange = (data[1] & 1) != 0;
    out.speed = highSpeedRange ? 63.75f + data[3] * 0.75f : data[3] * 0.25f;
    out.speedValid = !(highSpeedRange && data[3] == 255);
  }
  return true;
}

void merge(const Observation& value, Observation& out) {
  out.version = value.version;
  if (value.hasId) {
    out.hasId = true;
    out.idType = value.idType;
    out.aircraftType = value.aircraftType;
    memcpy(out.id, value.id, sizeof(out.id));
  }
  if (value.hasLocation) {
    out.hasLocation = true;
    out.positionValid = value.positionValid;
    out.altitudeValid = value.altitudeValid;
    out.speedValid = value.speedValid;
    out.status = value.status;
    out.latitude = value.latitude;
    out.longitude = value.longitude;
    out.altitude = value.altitude;
    out.speed = value.speed;
  }
}
}

bool decodeMessage(const uint8_t* data, size_t length, Observation& out) {
  if (!data || !length) return false;
  Observation result;
  if ((data[0] >> 4) == 15) {
    if ((data[0] & 15) > kLastProtocolVersion || length < 3 ||
        data[1] != kMessageBytes || data[2] == 0 ||
        data[2] > kMaxPackedMessages ||
        length != 3u + static_cast<size_t>(data[2]) * kMessageBytes) return false;
    for (size_t i = 0; i < data[2]; ++i) {
      if (!decodeSingle(data + 3 + i * kMessageBytes, result)) return false;
    }
  } else {
    if (length != kMessageBytes || !decodeSingle(data, result)) return false;
  }
  if (!result.hasId && !result.hasLocation) return false;
  out = result;
  return true;
}

bool parseBleAdvertisement(const uint8_t* data, size_t length, Observation& out) {
  if (!data || length > 1650) return false;
  Observation result;
  for (size_t offset = 0; offset < length;) {
    const size_t fieldLength = data[offset++];
    if (!fieldLength) break;
    if (fieldLength > length - offset) return false;
    // AD type 0x16, UUID FFFA little endian, application code 0x0D,
    // one message counter byte, then the ODID message/pack.
    if (fieldLength >= 5 && data[offset] == 0x16 &&
        data[offset + 1] == 0xfa && data[offset + 2] == 0xff &&
        data[offset + 3] == 0x0d) {
      Observation value;
      if (!decodeMessage(data + offset + 5, fieldLength - 5, value)) return false;
      merge(value, result);
    }
    offset += fieldLength;
  }
  if (!result.hasId && !result.hasLocation) return false;
  out = result;
  return true;
}

bool parseWifiBeacon(const uint8_t* data, size_t length, Observation& out) {
  if (!data || length < 36 || length > 2352 || data[0] != 0x80 ||
      (data[1] & 0x40)) return false;
  Observation result;
  size_t offset = 36;  // 24-byte MAC header + 12-byte beacon fixed fields.
  while (offset < length) {
    if (length - offset < 2) return false;
    const uint8_t tag = data[offset++];
    const size_t fieldLength = data[offset++];
    if (fieldLength > length - offset) return false;
    if (tag == 221 && fieldLength >= 5 &&
        data[offset] == 0xfa && data[offset + 1] == 0x0b &&
        data[offset + 2] == 0xbc && data[offset + 3] == 0x0d) {
      Observation value;
      if (!decodeMessage(data + offset + 5, fieldLength - 5, value)) return false;
      merge(value, result);
    }
    offset += fieldLength;
  }
  if (!result.hasId && !result.hasLocation) return false;
  out = result;
  return true;
}
}
