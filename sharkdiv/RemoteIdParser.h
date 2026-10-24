#pragma once

// Receive-only ASTM Remote ID adapter. No Arduino or radio dependencies:
// callers supply complete advertisement/beacon bytes and own radio lifetime.
#include <stddef.h>
#include <stdint.h>

namespace RemoteId {
struct Observation {
  bool hasId = false;
  bool hasLocation = false;
  bool positionValid = false;
  bool altitudeValid = false;
  bool speedValid = false;
  uint8_t idType = 0;
  uint8_t aircraftType = 0;
  uint8_t status = 0;
  uint8_t version = 0;
  char id[41] = {};  // 20 printable bytes or 20 bytes rendered as hex.
  double latitude = 0;
  double longitude = 0;
  float altitude = 0;  // WGS84 geodetic altitude, metres.
  float speed = 0;     // Horizontal speed, m/s.
};

// Decode a complete 25-byte message or 3 + N*25 byte pack (N <= 9).
// Only Basic ID and aircraft Location are surfaced. Other defined messages
// are skipped. Unsupported versions, malformed packs and unknown types fail.
// Every failure leaves out unchanged.
bool decodeMessage(const uint8_t* data, size_t length, Observation& out);
bool parseBleAdvertisement(const uint8_t* data, size_t length, Observation& out);
// Input is an 802.11 beacon, WITHOUT trailing FCS. Supports ASTM vendor IE
// FA:0B:BC / 0D; Wi-Fi NAN and proprietary DroneID are not parsed.
bool parseWifiBeacon(const uint8_t* data, size_t length, Observation& out);
}
