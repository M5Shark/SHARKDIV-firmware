#include "../sharkdiv/FlockSignatures.h"
#include "../sharkdiv/flock_hunter.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <string>

static void appendAd(std::vector<uint8_t>& data, uint8_t type, const std::vector<uint8_t>& value) {
  data.push_back(uint8_t(value.size() + 1));
  data.push_back(type);
  data.insert(data.end(), value.begin(), value.end());
}
static std::vector<uint8_t> nameAd(const std::string& name) {
  std::vector<uint8_t> data;
  appendAd(data, 0x09, {name.begin(), name.end()});
  return data;
}
static uint8_t ble(const std::vector<uint8_t>& data) {
  return FlockSignatures::bleAdvertisementEvidence(data.data(), data.size());
}

static void signaturesCoverPublishedCandidates() {
  using namespace FlockSignatures;
  static_assert(kWifiOuiCount == 34, "Published union list must stay complete");
  for (const auto& oui : kWifiOuis) {
    const uint8_t mac[] = {oui[0], oui[1], oui[2], 1, 2, 3};
    assert(wifiOui(mac));
  }
  assert(!wifiOui(nullptr));
  const uint8_t placeholder[] = {0x60,0xd0,0xe9,1,2,3};
  const uint8_t multicast[] = {0x83,0x6b,0xf2,1,2,3};
  assert(!wifiOui(placeholder) && !wifiOui(multicast));
  assert(ssid("Flock") && ssid("fLoCk-A0b123"));
  assert(!ssid(nullptr) && !ssid("Flock-notacamera") && !ssid("My Flock-001122"));
  assert(!ssid("Flock-00112Z") && !ssid("Flock-0011223"));
  for (const char* name : {"FLOCK", "Raven-123", "Penguin-1234567890", "FS Ext Battery"})
    assert(ble(nameAd(name)) == ByName);
  for (const char* name : {"Ravenclaw", "BlackFlock", "Penguin-1234", "DfuTarg", "1234567890"})
    assert(ble(nameAd(name)) == 0);
  auto generic = nameAd("1234567890");
  appendAd(generic, 0xff, {0xc8,0x09,0x01});
  assert(ble(generic) == (ByName | ByManufacturer));
  std::vector<uint8_t> nameless;
  appendAd(nameless, 0xff, {0xc8,0x09});
  assert(ble(nameless) == ByManufacturer);
  std::vector<uint8_t> service;
  appendAd(service, 0x07, {0x6f,0x2e,0x17,0xdf,0x14,0x18,0xe5,0x9f,
                         0xa8,0x46,0x32,0x95,0x38,0xbb,0xcc,0xe8});
  assert(ble(service) == ByService);
  for (uint8_t high = 0x31; high <= 0x35; ++high) {
    service.clear(); appendAd(service, 0x03, {0, high});
    assert(ble(service) == ByService);
  }
  service.clear(); appendAd(service, 0x03, {1, 0x31});
  assert(ble(service) == 0);  // Characteristic 3101 is not advertised service 3100.
}

static void malformedAdvertisementsNeverBecomeHits() {
  using namespace FlockSignatures;
  assert(bleAdvertisementEvidence(nullptr, 31) == 0);
  const auto good = nameAd("Penguin-1234567890");
  for (size_t length = 0; length < good.size(); ++length) {
    const std::vector<uint8_t> truncated(good.begin(), good.begin() + length);
    assert(ble(truncated) == 0);
  }
  auto malformed = good;
  malformed.insert(malformed.end(), {5, 0xff, 0xc8, 0x09});
  assert(ble(malformed) == 0);
  malformed = good;
  appendAd(malformed, 0x03, {0,0x31,0});
  assert(ble(malformed) == 0);
  malformed = good;
  appendAd(malformed, 0x07, std::vector<uint8_t>(15));
  assert(ble(malformed) == 0);
}

static void probeParserRejectsTruncationAndSupportsLocalAddresses() {
  using namespace FlockSignatures;
  const std::vector<uint8_t> body = {
    0,0, 2,0, 12,0, 127,0, 221,7,0x50,0x6f,0x9a,0x16,0x03,0x01,0x03,
    45,0, 191,0, 221,7,0x00,0x50,0xf2,0x08,0,0,0
  };
  assert(probeBody(body.data(), body.size()).fingerprint);
  for (size_t size = 0; size < body.size(); ++size) {
    const std::vector<uint8_t> truncated(body.begin(), body.begin() + size);
    assert(!probeBody(truncated.data(), truncated.size()).fingerprint);
  }
  auto malformed = body; malformed.push_back(221);
  assert(!probeBody(malformed.data(), malformed.size()).valid);
  malformed = body; malformed.insert(malformed.end(), {221,7,0});
  assert(!probeBody(malformed.data(), malformed.size()).valid);
  malformed = body; malformed.insert(malformed.end(), {0,0});
  assert(!probeBody(malformed.data(), malformed.size()).valid);
  std::vector<uint8_t> frame(24, 0);
  frame[0] = 0x40;
  frame[10] = 0x82; frame[11] = 0x6b; frame[12] = 0xf2;
  frame.insert(frame.end(), body.begin(), body.end());
  assert(probeFrame(frame.data(), frame.size(), false).fingerprint);
  frame.insert(frame.end(), {0xde,0xad,0xbe,0xef});
  auto result = probeFrame(frame.data(), frame.size());
  assert(result.valid && result.oui && result.wildcard && result.fingerprint);
  assert(FlockHunter::isFlockProbe(reinterpret_cast<const FlockHunter::MacHdr*>(frame.data()),
                                  frame.data()+24, int(frame.size()-24)));
  frame[10] = 0x02; frame[11] = 0x99;
  result = probeFrame(frame.data(), frame.size());
  assert(result.valid && !result.oui && result.fingerprint);  // Distinct evidence survives rotation.
  frame[10] = 0x03;
  assert(!probeFrame(frame.data(), frame.size()).valid);
  frame[10] = 0x02; frame[0] = 0x80;
  assert(!probeFrame(frame.data(), frame.size()).valid);
  frame[0] = 0x40; frame[1] = 1;
  assert(!probeFrame(frame.data(), frame.size()).valid);
  assert(!probeFrame(nullptr, 100).valid);
  for (size_t length = 0; length < 28; ++length) {
    const std::vector<uint8_t> tiny(length, 0);
    assert(!probeFrame(tiny.data(), tiny.size()).valid);
  }
}

int main() {
  signaturesCoverPublishedCandidates();
  malformedAdvertisementsNeverBecomeHits();
  probeParserRejectsTruncationAndSupportsLocalAddresses();
  puts("Flock radio signature tests passed");
}
