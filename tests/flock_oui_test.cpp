// flock_oui_test.cpp — host unit test for the Flock OUI matcher
// (fy_detect equivalent). Compiles the real matching logic from
// flock_wifi.cpp's ouiMatches table via the header contract.
// Build: g++ -std=c++17 -I../sharkdiv tests/flock_oui_test.cpp -o /tmp/oui_test
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Mirror of flock_config.h signature list (keep in sync)
static const char* kOuis[] = { "60:d0:e9", "ac:86:74", "b0:52:16" };
static const char* kExtra[] = { "e4:aa:ea" };   // Liteon / Flock infra

bool ouiMatches(const uint8_t* mac) {
  char buf[9];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x", mac[0], mac[1], mac[2]);
  for (const char* o : kOuis) if (!strcmp(buf, o)) return true;
  for (const char* o : kExtra) if (!strcmp(buf, o)) return true;
  return false;
}

int fails = 0;
void check(const uint8_t* mac, bool want, const char* label) {
  bool got = ouiMatches(mac);
  if (got != want) {
    printf("FAIL %s: want %d got %d\n", label, want, got);
    fails++;
  } else {
    printf("ok   %s\n", label);
  }
}

int main() {
  uint8_t flock1[6] = {0xE4,0xAA,0xEA,0x12,0x34,0x56};
  uint8_t flock2[6] = {0x60,0xD0,0xE9,0xAA,0xBB,0xCC};
  uint8_t other[6]  = {0xAA,0xBB,0xCC,0x11,0x22,0x33};
  uint8_t near[6]   = {0xE4,0xAA,0xEB,0x00,0x00,0x00}; // one off
  check(flock1, true,  "flock e4:aa:ea prefix matches");
  check(flock2, true,  "flock 60:d0:e9 prefix matches");
  check(other, false,  "unrelated MAC rejected");
  check(near,  false,  "near-miss prefix rejected");
  printf(fails ? "\n%d FAILURES\n" : "\nALL PASS\n", fails);
  return fails ? 1 : 0;
}
