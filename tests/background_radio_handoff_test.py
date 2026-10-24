#!/usr/bin/env python3
"""Exercise the production background loops while a foreground tool opens."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


wifi = (root / "sharkdiv/wifi.cpp").read_text()
ble = (root / "sharkdiv/bluetooth.cpp").read_text()
common = r'''
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
constexpr int portMAX_DELAY = 0, portTICK_PERIOD_MS = 1;
#define pdMS_TO_TICKS(ms) (ms)
struct EndIteration {};
void vTaskDelay(uint32_t) { throw EndIteration{}; }
uint32_t millis() { return 65000; }
std::atomic<bool> foregroundWaiting{false};
thread_local bool isForeground = false;
void xSemaphoreTake(std::mutex* m, int) {
  if (isForeground) foregroundWaiting = true;
  m->lock();
}
void xSemaphoreGive(std::mutex* m) { m->unlock(); }
struct Barrier {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false, released = false;
  void block() {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true; cv.notify_all();
    cv.wait(lock, [&] { return released; });
  }
  void waitEntered() {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return entered; });
  }
  void release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true; cv.notify_all();
  }
};
template <typename F> void oneIteration(F f) {
  try { f(nullptr); } catch (const EndIteration&) {}
}
'''
wifi_fixture = r'''
namespace WifiTest {
std::mutex mutex;
std::mutex* bgScanMutex = &mutex;
std::atomic<bool> feature_active{false};
bool in_sub_menu = false, fgWifiScanInProgress = false;
bool bgHasResults = false, bgScanRunning = false;
uint32_t bgBootMs = 1, bgLastScanMs = 0;
constexpr uint32_t BG_BOOT_GRACE_MS = 6000, BG_SCAN_INTERVAL_MS = 15000;
constexpr int WIFI_SCAN_RUNNING = -1, WIFI_SCAN_FAILED = -2, WIFI_STA = 1;
struct Settings { bool autoWifiScan = true; } config;
Settings& settings() { return config; }
uint32_t wifiStaScanMsPerChannel() { return 120; }
Barrier scanStart;
std::atomic<int> stops{0};
int scanStatus = WIFI_SCAN_FAILED;
struct Wifi {
  void mode(int) {} void disconnect() {} void scanDelete() {}
  int scanComplete() { return scanStatus; }
  int scanNetworks(bool, bool, bool, uint32_t) {
    scanStart.block();
    return scanStatus = WIFI_SCAN_RUNNING;
  }
} WiFi;
int esp_wifi_scan_stop() { ++stops; scanStatus = WIFI_SCAN_FAILED; return 0; }
'''
ble_fixture = r'''
namespace BleTest {
std::mutex mutex;
std::mutex* bgBleScanMutex = &mutex;
std::atomic<bool> feature_active{false};
bool in_sub_menu = false, fgBleScanInProgress = false;
bool bgHasResults = false, bgBleScanRunning = false, isScanning = false;
bool bleInitDone = false, initSucceeds = true;
uint32_t bgBootMs = 1, bgLastScanMs = 0;
constexpr uint32_t BG_BOOT_GRACE_MS = 6000, BG_BLE_SCAN_INTERVAL_MS = 15000;
struct Settings { bool autoBleScan = true; } config;
Settings& settings() { return config; }
Barrier scanStart;
struct Results { int getCount() const { return 2; } } bleResults;
std::atomic<int> stops{0}, starts{0};
struct Scan {
  Results start(int, bool) { ++starts; scanStart.block(); return {}; }
  void stop() { ++stops; }
} scanner;
Scan* bleScan = nullptr;
void ensureBleInit() {
  if (initSucceeds) { bleScan = &scanner; bleInitDone = true; }
}
'''
checks = r'''
int main() {
  {
    using namespace WifiTest;
    std::thread bg([] { oneIteration(bgWifiScanTask); });
    scanStart.waitEntered(); // Background already passed the idle check.
    feature_active = true;
    std::atomic<bool> acquired{false};
    std::thread ui([&] {
      isForeground = true;
      waitForBackgroundScanner();
      acquired = true;
    });
    while (!foregroundWaiting.load()) std::this_thread::yield();
    assert(!acquired && stops == 0);
    scanStart.release();
    bg.join(); ui.join();
    assert(acquired && stops == 1 && !bgScanRunning);
    scanStatus = WIFI_SCAN_RUNNING; // Foreground now owns a new scan.
    oneIteration(bgWifiScanTask);
    assert(stops == 1 && scanStatus == WIFI_SCAN_RUNNING);
  }
  foregroundWaiting = false;
  {
    using namespace BleTest;
    std::thread bg([] { oneIteration(bgBleScanTask); });
    scanStart.waitEntered();
    feature_active = true;
    std::atomic<bool> acquired{false};
    std::thread ui([&] {
      isForeground = true;
      waitForBackgroundScanner();
      acquired = true;
    });
    while (!foregroundWaiting.load()) std::this_thread::yield();
    assert(!acquired);
    scanStart.release();
    bg.join(); ui.join();
    assert(acquired && !bgBleScanRunning && !isScanning);
    oneIteration(bgBleScanTask);
    assert(stops == 0 && starts == 1);
    // A failed stack initialization must never dereference a null scan object.
    feature_active = false; initSucceeds = false;
    bleInitDone = false; bleScan = nullptr;
    oneIteration(bgBleScanTask);
    assert(starts == 1);
  }
  puts("Background radio handoff tests passed");
}
'''
source = common + wifi_fixture
for signature in ("static void stopBgWifiScanIfRunning()", "static void bgWifiScanTask(void* )", "void waitForBackgroundScanner()"):
    source += function(wifi, signature) + "\n"
source += "}\n" + ble_fixture
for signature in ("static void stopBgBleScanIfRunning()", "static void bgBleScanTask(void* )", "void waitForBackgroundScanner()"):
    source += function(ble, signature) + "\n"
source += "}\n" + checks
with tempfile.TemporaryDirectory(prefix="sharkdiv-radio-handoff-") as directory:
    cpp = Path(directory) / "handoff.cpp"
    binary = Path(directory) / "handoff"
    cpp.write_text(source)
    subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
        "-std=c++11", "-pthread", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", str(cpp), "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True, timeout=15)
