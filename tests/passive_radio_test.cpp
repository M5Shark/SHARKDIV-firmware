#include "../sharkdiv/FindMyScanner.h"
#include "../sharkdiv/RfOccupancy.h"

#include <assert.h>
#include <stdio.h>
#include <vector>

static void parserRejectsMalformedFrames() {
  uint8_t status = 0xaa;
  uint8_t valid[29] = {0x4c, 0x00, 0x12, 0x19, 0x80};
  assert(!FindMyScanner::parseManufacturerData(nullptr, 29, status));
  assert(status == 0xaa);
  // Exact allocations make a premature header/payload read visible to ASan.
  for (size_t size = 0; size < sizeof(valid); ++size) {
    std::vector<uint8_t> truncated(valid, valid + size);
    assert(!FindMyScanner::parseManufacturerData(truncated.data(), size, status));
    assert(status == 0xaa);
  }
  assert(FindMyScanner::parseManufacturerData(valid, sizeof(valid), status));
  assert(status == 0x80);
  for (size_t headerByte = 0; headerByte < 4; ++headerByte) {
    const uint8_t original = valid[headerByte];
    for (unsigned value = 0; value < 256; ++value) {
      valid[headerByte] = uint8_t(value);
      assert(FindMyScanner::parseManufacturerData(valid, sizeof(valid), status) ==
             (value == original));
    }
    valid[headerByte] = original;
  }
  uint8_t pairing[29] = {0x4c, 0, 0x07, 0x19, 0x05};
  assert(!FindMyScanner::parseManufacturerData(pairing, sizeof(pairing), status));
  std::vector<uint8_t> overlong(valid, valid + sizeof(valid));
  overlong.push_back(0);
  assert(!FindMyScanner::parseManufacturerData(overlong.data(), overlong.size(), status));
}

static void tableIsBoundedAndExpiresAcrossClockWrap() {
  FindMyScanner::CandidateTable<2> table;
  const uint8_t a[6] = {1}, b[6] = {2}, c[6] = {3};
  table.observe(a, -10, 1, 100);
  table.observe(b, -90, 2, 200);
  table.observe(b, -80, 3, 300);
  assert(table.count == 2);
  assert(table.records[1].hits == 2 && table.records[1].rssi == -80);
  table.observe(c, -100, 0, 400);  // Weak but recent replaces stale strong entry.
  assert(table.count == 2 && table.records[0].mac[0] == 3);
  assert(!table.expire(499, 200));
  assert(table.expire(500, 200));
  assert(table.count == 1 && table.records[0].mac[0] == 3);
  table.records[0].hits = UINT16_MAX;
  table.observe(c, -50, 0, 501);
  assert(table.records[0].hits == UINT16_MAX);
  table.clear();
  assert(table.count == 0);
  table.observe(a, -50, 0, UINT32_MAX - 50);
  table.observe(b, -60, 0, UINT32_MAX - 20);
  table.observe(c, -80, 0, 20);
  assert(table.records[0].mac[0] == 3);  // Wrapped time still evicts oldest.
  assert(!table.expire(78, 100));
  assert(table.expire(79, 100));
  assert(table.count == 1 && table.records[0].mac[0] == 3);
}

static void occupancyUsesSamplesNotSignalPower() {
  assert(RfOccupancy::percent(0, 10) == 0);
  assert(RfOccupancy::percent(1, 10) == 10);
  assert(RfOccupancy::percent(5, 10) == 50);
  assert(RfOccupancy::percent(25, 50) == 50);
  assert(RfOccupancy::percent(50, 100) == 50);
  assert(RfOccupancy::percent(10, 10) == 100);
  assert(RfOccupancy::percent(UINT16_MAX, UINT16_MAX) == 100);
  assert(RfOccupancy::percent(1, 0) == 0);
  assert(RfOccupancy::percent(11, 10) == 100);
  assert(RfOccupancy::aboveBaseline(30, 40) == 0);
  assert(RfOccupancy::aboveBaseline(40, 40) == 0);
  assert(RfOccupancy::aboveBaseline(70, 40) == 30);
}

int main() {
  parserRejectsMalformedFrames();
  tableIsBoundedAndExpiresAcrossClockWrap();
  occupancyUsesSamplesNotSignalPower();
  puts("Passive radio tests passed");
}
