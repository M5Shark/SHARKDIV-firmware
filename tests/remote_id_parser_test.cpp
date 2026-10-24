#include "../sharkdiv/RemoteIdParser.h"
#include "reference/opendroneid/opendroneid.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using Bytes = std::vector<uint8_t>;

static Bytes basic() {
  // Independent on-wire fixture: v2, Basic ID, serial number + multirotor.
  Bytes result(25, 0);
  result[0] = 0x02;
  result[1] = 0x12;
  memcpy(result.data() + 2, "TEST-AIRCRAFT-123", 17);
  return result;
}

static Bytes location() {
  // Independent on-wire fixture: airborne, 90 degrees, 10 m/s,
  // 37.5N / 122.25W, 100 m WGS84. No radio transmission occurs.
  Bytes result(25, 0);
  result[0] = 0x12;
  result[1] = 0x20;
  result[2] = 90;
  result[3] = 40;
  const int32_t lat = 375000000;
  const int32_t lon = -1222500000;
  for (int i = 0; i < 4; ++i) {
    result[5 + i] = (static_cast<uint32_t>(lat) >> (i * 8)) & 255;
    result[9 + i] = (static_cast<uint32_t>(lon) >> (i * 8)) & 255;
  }
  // Altitude encoding is (altitude + 1000) / 0.5.
  result[15] = 2200 & 255;
  result[16] = 2200 >> 8;
  return result;
}

static Bytes pack() {
  Bytes result{0xf2, 25, 2};
  const Bytes id = basic(), loc = location();
  result.insert(result.end(), id.begin(), id.end());
  result.insert(result.end(), loc.begin(), loc.end());
  return result;
}

static Bytes ble(const Bytes& message) {
  Bytes result{static_cast<uint8_t>(message.size() + 5), 0x16, 0xfa, 0xff, 0x0d, 42};
  result.insert(result.end(), message.begin(), message.end());
  return result;
}

static Bytes wifi(const Bytes& message) {
  Bytes result(36, 0);
  result[0] = 0x80;
  // Empty SSID before Remote ID verifies that a legal zero-length IE is skipped.
  result.push_back(0);
  result.push_back(0);
  result.insert(result.end(), {221, static_cast<uint8_t>(message.size() + 5),
                               0xfa, 0x0b, 0xbc, 0x0d, 42});
  result.insert(result.end(), message.begin(), message.end());
  return result;
}

static void verifyLocation(const RemoteId::Observation& out) {
  assert(out.hasLocation && out.positionValid && out.altitudeValid && out.speedValid);
  assert(fabs(out.latitude - 37.5) < 1e-7);
  assert(fabs(out.longitude + 122.25) < 1e-7);
  assert(fabs(out.altitude - 100) < 0.01);
  assert(fabs(out.speed - 10) < 0.01);
  assert(out.status == 2);
}

int main() {
  RemoteId::Observation out;
  // Upstream core is a HOST-TEST REFERENCE ONLY. Its public encoder supplies
  // independent fixtures across the low/high speed ranges and geographic
  // extremes, then its decoder gives comparison values for the original
  // production byte parser. No reference source is in the Arduino sketch tree.
  const double latitudes[] = {-90, -12.3456789, 0, 37.5, 90};
  const double longitudes[] = {-180, -122.25, 0, 15.1234567, 180};
  const float speeds[] = {0, 10, 63.75f, 64.5f, 100, 254.25f, 255};
  for (double latitude : latitudes) for (double longitude : longitudes) {
    for (float speed : speeds) {
      ODID_Location_data source, reference;
      ODID_Location_encoded encoded;
      odid_initLocationData(&source);
      source.Status = ODID_STATUS_AIRBORNE;
      source.Latitude = latitude;
      source.Longitude = longitude;
      source.AltitudeGeo = 123.5f;
      source.SpeedHorizontal = speed;
      assert(encodeLocationMessage(&encoded, &source) == ODID_SUCCESS);
      assert(decodeLocationMessage(&reference, &encoded) == ODID_SUCCESS);
      assert(RemoteId::decodeMessage(reinterpret_cast<const uint8_t*>(&encoded),
                                     sizeof(encoded), out));
      assert(fabs(out.latitude - reference.Latitude) < 1e-7);
      assert(fabs(out.longitude - reference.Longitude) < 1e-7);
      assert(fabs(out.altitude - reference.AltitudeGeo) < 0.001);
      assert(fabs(out.speed - reference.SpeedHorizontal) < 0.001);
      assert(out.speedValid == (reference.SpeedHorizontal != INV_SPEED_H));
    }
  }
  for (unsigned version = 0; version <= 2; ++version) {
    ODID_BasicID_data source, reference;
    ODID_BasicID_encoded encoded;
    odid_initBasicIDData(&source);
    source.IDType = ODID_IDTYPE_SERIAL_NUMBER;
    source.UAType = ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
    strcpy(source.UASID, "REFERENCE-AIRCRAFT");
    assert(encodeBasicIDMessage(&encoded, &source) == ODID_SUCCESS);
    encoded.ProtoVersion = version;
    assert(decodeBasicIDMessage(&reference, &encoded) == ODID_SUCCESS);
    assert(RemoteId::decodeMessage(reinterpret_cast<const uint8_t*>(&encoded),
                                   sizeof(encoded), out));
    assert(!strcmp(out.id, reference.UASID) && out.version == version);
  }
  Bytes data = basic();
  assert(RemoteId::decodeMessage(data.data(), data.size(), out));
  assert(out.hasId && !out.hasLocation && out.idType == 1 && out.aircraftType == 2);
  assert(!strcmp(out.id, "TEST-AIRCRAFT-123"));
  data = location();
  assert(RemoteId::decodeMessage(data.data(), data.size(), out));
  verifyLocation(out);
  data = pack();
  assert(RemoteId::decodeMessage(data.data(), data.size(), out));
  assert(out.hasId);
  verifyLocation(out);
  data = ble(basic());
  assert(RemoteId::parseBleAdvertisement(data.data(), data.size(), out));
  assert(out.hasId);
  data = ble(location());
  assert(RemoteId::parseBleAdvertisement(data.data(), data.size(), out));
  verifyLocation(out);
  data = wifi(pack());
  assert(RemoteId::parseWifiBeacon(data.data(), data.size(), out));
  assert(out.hasId);
  verifyLocation(out);

  // Every prefix of a complete single-message transport must fail. An output
  // sentinel ensures rejection cannot partially replace a previously good hit.
  const Bytes formats[] = {basic(), ble(basic()), wifi(pack())};
  for (int kind = 0; kind < 3; ++kind) {
    const Bytes& bytes = formats[kind];
    for (size_t length = 0; length < bytes.size(); ++length) {
      out = RemoteId::Observation{};
      out.id[0] = 'Q';
      bool ok = kind == 0 ? RemoteId::decodeMessage(bytes.data(), length, out) :
                kind == 1 ? RemoteId::parseBleAdvertisement(bytes.data(), length, out) :
                            RemoteId::parseWifiBeacon(bytes.data(), length, out);
      assert(!ok && out.id[0] == 'Q' && !out.hasId && !out.hasLocation);
    }
  }
  assert(!RemoteId::decodeMessage(nullptr, 25, out));
  assert(!RemoteId::parseBleAdvertisement(nullptr, 31, out));
  assert(!RemoteId::parseWifiBeacon(nullptr, 80, out));
  data = basic(); data[0] = 0x62; // Unknown message type.
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = basic(); data[0] = 0x03; // Future protocol version is not claimed.
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = basic(); data[1] = 0x02; // No Basic ID type.
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = basic(); memset(data.data() + 2, 0, 20); // Empty identity.
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = basic(); data[1] = 0x32; // Binary UUID rendered as bounded hex.
  assert(RemoteId::decodeMessage(data.data(), data.size(), out));
  assert(strlen(out.id) == 40 && out.id[0] == '5' && out.id[1] == '4');
  data = basic(); data[3] = 1; // Control byte never reaches the display.
  assert(RemoteId::decodeMessage(data.data(), data.size(), out));
  assert(out.id[1] == '?');
  data = pack(); data[1] = 24;
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = pack(); data[2] = 0;
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = pack(); data[2] = 10;
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = pack(); data[28] = 0xf2; // Nested pack.
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = pack(); data[28] = 0x62; // Unknown second member, atomic rejection.
  out = RemoteId::Observation{};
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out) && !out.hasId);
  data = location(); data[8] = 0x7f; // Latitude exceeds 90 degrees.
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = location(); data[1] = 0xf0;
  assert(!RemoteId::decodeMessage(data.data(), data.size(), out));
  data = location();
  memset(data.data() + 5, 0, 12); data[3] = 255; data[1] |= 1;
  assert(RemoteId::decodeMessage(data.data(), data.size(), out));
  assert(!out.positionValid && !out.altitudeValid && !out.speedValid);
  data = ble(basic()); data[2] = 0xfb; // Another service is not Remote ID.
  assert(!RemoteId::parseBleAdvertisement(data.data(), data.size(), out));
  data = ble(basic()); data[4] = 0x0e;
  assert(!RemoteId::parseBleAdvertisement(data.data(), data.size(), out));
  data = ble(basic()); data.push_back(255); // Malformed trailing AD element.
  assert(!RemoteId::parseBleAdvertisement(data.data(), data.size(), out));
  data = wifi(pack()); data[40] = 0x60; // Old false-positive company heuristic.
  assert(!RemoteId::parseWifiBeacon(data.data(), data.size(), out));
  data = wifi(pack()); data[0] = 0x40; // Probe request is not a beacon.
  assert(!RemoteId::parseWifiBeacon(data.data(), data.size(), out));
  data = wifi(pack()); data[1] = 0x40; // Protected frame.
  assert(!RemoteId::parseWifiBeacon(data.data(), data.size(), out));
  data = wifi(pack()); data.push_back(1); // Truncated trailing IE header.
  assert(!RemoteId::parseWifiBeacon(data.data(), data.size(), out));

  // Deterministic hostile-input smoke test, most useful with ASan/UBSan.
  uint32_t random = 1234567;
  for (size_t length = 0; length < 512; ++length) {
    Bytes bytes(length);
    for (uint8_t& value : bytes) {
      random = random * 1664525u + 1013904223u;
      value = random >> 24;
    }
    RemoteId::decodeMessage(bytes.data(), bytes.size(), out);
    RemoteId::parseBleAdvertisement(bytes.data(), bytes.size(), out);
    RemoteId::parseWifiBeacon(bytes.data(), bytes.size(), out);
  }
  puts("Remote ID parser: fixtures, transports, truncation and malformed-input checks passed");
}
