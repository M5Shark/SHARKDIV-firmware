#include "../sharkdiv/DetectionAlertPolicy.h"
#include <cassert>
#include <cstdio>
using namespace DetectionAlerts;
int main() {
  Policy policy;
  assert(policy.offer(Source::WiFi, 1, 0));
  assert(!policy.offer(Source::WiFi, 1, 1000));
  assert(!policy.offer(Source::WiFi, 1, 29000));
  assert(!policy.offer(Source::WiFi, 1, 58000)); // continuing reception stays quiet
  assert(policy.offer(Source::WiFi, 1, 88000)); // returns after 30 seconds absent
  assert(!policy.offer(Source::BLE, 2, 88100)); // burst coalescing across tools
  assert(!policy.offer(Source::BLE, 2, 89100)); // a burst-suppressed device stays seen
  assert(policy.offer(Source::BLE, 3, 89100));
  assert(!policy.offer(Source::Count, 5, 99999));
  policy.reset();
  assert(policy.offer(Source::WiFi, 1, UINT32_MAX - 500));
  assert(!policy.offer(Source::WiFi, 2, 100));
  assert(policy.offer(Source::WiFi, 3, 501)); // unsigned time rollover
  assert(!policy.offer(Source::WiFi, 1, 600));
  assert(policy.offer(Source::WiFi, 1, 30600));
  policy.reset();
  for (uint32_t i = 0; i < 10000; ++i) assert(policy.offer(Source::WiFi, i, i * 1000));
  assert(!policy.offer(Source::WiFi, 9999, 10000000));
  assert(policy.offer(Source::GPS, 9999, 10001000)); // independent identity domains
  policy.reset();
  int crowdedAlerts = 0;
  for (uint32_t round = 1; round <= 100; ++round)
    for (uint32_t device = 0; device < 100; ++device)
      crowdedAlerts += policy.offer(Source::BLE, device, round * 1000);
  assert(crowdedAlerts == 1); // full live history must not churn forever
  assert(policy.offer(Source::BLE, 200, 131000)); // expired slots are reusable
  puts("Detection alert deduplication, burst, capacity and clock tests passed");
}
