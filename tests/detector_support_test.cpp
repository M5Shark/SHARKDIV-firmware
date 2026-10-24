#include "../sharkdiv/DetectorSupport.h"
#include "../sharkdiv/FlockSignatures.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace DetectorSupport;
using Bytes = std::vector<uint8_t>;
static Bytes frame(uint8_t subtype, const char* ssid) {
  Bytes result(subtype == 4 ? 24 : 36, 0);
  result[0] = subtype << 4;
  const uint8_t mac[] = {0xb4, 0x1e, 0x52, 1, 2, 3};
  memcpy(result.data() + 10, mac, 6);
  const size_t length = strlen(ssid);
  result.push_back(0);
  result.push_back(length);
  result.insert(result.end(), ssid, ssid + length);
  return result;
}
int main() {
  OuiList prefixes;
  assert(prefixes.add("B4:1E:52") && prefixes.count == 1);
  assert(prefixes.add("b4:1e:52") && prefixes.count == 1);
  assert(prefixes.add("82:6b:f2")); // Known locally administered Flock prefix.
  assert(!prefixes.add("zz:00:00"));
  assert(!prefixes.add("b4:1e:52:00:01:02"));
  assert(!prefixes.add("b4-1e-52"));
  assert(!prefixes.add("01:00:00")); // Multicast.
  const uint8_t mac[] = {0xb4,0x1e,0x52,1,2,3};
  assert(prefixes.matches(mac));
  uint8_t untouched[3] = {1,2,3};
  assert(!parseOui("xx:xx:xx", untouched) && untouched[0] == 1);
  assert(!parseOui(nullptr, untouched));

  WifiAdvertisement advertisement;
  for (uint8_t subtype : {4, 5, 8}) {
    Bytes bytes = frame(subtype, "Flock-12ab3F");
    assert(parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
    assert(advertisement.probe == (subtype == 4));
    assert(!strcmp(advertisement.ssid, "Flock-12ab3F"));
    assert(FlockSignatures::wifiOui(advertisement.mac));
    assert(FlockSignatures::ssid(advertisement.ssid));
    // Every truncated prefix fails and cannot alter the previous observation.
    for (size_t len = 0; len < bytes.size(); ++len) {
      WifiAdvertisement output;
      output.ssid[0] = 'Q';
      assert(!parseWifiAdvertisement(bytes.data(), len, output));
      assert(output.ssid[0] == 'Q');
    }
  }
  Bytes bytes = frame(4, "");
  assert(parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  assert(advertisement.wildcard && advertisement.probe);
  bytes = frame(8, "");
  assert(parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  assert(!advertisement.wildcard && !advertisement.probe);
  bytes = frame(4, "Directed");
  assert(parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  assert(!advertisement.wildcard);
  bytes = frame(8, "not-a-camera");
  assert(parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  assert(!FlockSignatures::ssid(advertisement.ssid));
  bytes = frame(8, "Flock-abcdef");
  bytes.push_back(221); bytes.push_back(10); // Incomplete trailing vendor IE.
  assert(!parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  bytes = frame(8, "Flock-abcdef");
  bytes.insert(bytes.end(), {0,0}); // Ambiguous duplicate SSID.
  assert(!parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  bytes = frame(8, "Flock-abcdef"); bytes[1] = 0x40; // Protected frame.
  assert(!parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  bytes = frame(8, "Flock-abcdef"); bytes[0] = 0x08; // Data frame.
  assert(!parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  bytes = frame(8, "Flock-abcdef"); bytes[10] |= 1; // Multicast transmitter.
  assert(!parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  bytes = frame(8, "12345678901234567890123456789012345");
  assert(!parseWifiAdvertisement(bytes.data(), bytes.size(), advertisement));
  assert(!parseWifiAdvertisement(nullptr, 100, advertisement));

  Table table;
  table.observe("camera candidate", "b4:1e:52:01:02:03", -70, 6, Vendor, 100);
  table.observe("Flock-123456", "b4:1e:52:01:02:03", -55, 11, Name, 200);
  assert(table.count == 1 && table.entries[0].seen == 2);
  assert(table.entries[0].channel == 11 && table.entries[0].rssi == -55);
  assert(table.entries[0].firstMs == 100 && table.entries[0].lastMs == 200);
  assert(table.entries[0].evidence == (Vendor | Name));
  table.observe("BLE candidate", "b4:1e:52:01:02:03", -80, 0, Service, 300);
  assert(table.count == 2); // Same MAC on another transport remains separate.
  Hit snapshot[Table::capacity];
  assert(table.snapshot(snapshot, 1, 301) == 1);
  assert(table.snapshot(snapshot, Table::capacity, 30201) == 1);
  assert(snapshot[0].channel == 0);
  assert(table.snapshot(snapshot, Table::capacity, 30301) == 0);

  for (int i = 0; i < 20; ++i) {
    char address[18];
    snprintf(address, sizeof(address), "00:11:22:33:44:%02x", i);
    table.observe("candidate", address, -60, 1, Vendor, i + 1);
  }
  assert(table.count == Table::capacity);
  for (int i = 0; i < table.count; ++i) assert(table.entries[i].firstMs >= 7);
  table.clear();
  table.observe("before wrap", "00:11:22:33:44:55", -70, 6, Vendor, UINT32_MAX - 10);
  assert(table.snapshot(snapshot, Table::capacity, 20) == 1);
  assert(table.snapshot(snapshot, Table::capacity, 30000) == 0);
  table.clear();
  for (unsigned i = 0; i < 65540; ++i)
    table.observe("saturated", "00:11:22:33:44:55", -70, 0, Name, i);
  assert(table.entries[0].seen == 65535);
  puts("Detector support: WiFi frame parsing, OUI validation, bounded records and expiry passed");
}
