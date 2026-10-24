#!/usr/bin/env python3
"""Exercise the real host-operation helper with a serialized mock BLE host."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#pragma once
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
class NimBLEDevice {
 public:
  static bool initialized;
  static bool getInitialized() { return initialized; }
};
struct StaticSemaphore_t {
  std::mutex mutex;
  std::condition_variable ready;
  bool signalled = false;
};
using SemaphoreHandle_t = StaticSemaphore_t*;
constexpr unsigned portMAX_DELAY = ~0u;
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t* storage) {
  return storage;
}
inline void xSemaphoreGive(SemaphoreHandle_t semaphore) {
  std::lock_guard<std::mutex> lock(semaphore->mutex);
  semaphore->signalled = true;
  semaphore->ready.notify_one();
}
inline void xSemaphoreTake(SemaphoreHandle_t semaphore, unsigned) {
  std::unique_lock<std::mutex> lock(semaphore->mutex);
  semaphore->ready.wait(lock, [&] { return semaphore->signalled; });
}
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
struct ble_npl_event {
  void (*callback)(ble_npl_event*) = nullptr;
  void* argument = nullptr;
};
struct ble_npl_eventq {
  std::mutex mutex;
  std::condition_variable ready;
  std::deque<ble_npl_event*> events;
};
extern ble_npl_eventq queue;
inline void ble_npl_event_init(ble_npl_event* event,
                               void (*callback)(ble_npl_event*), void* argument) {
  event->callback = callback;
  event->argument = argument;
}
inline void* ble_npl_event_get_arg(ble_npl_event* event) { return event->argument; }
inline ble_npl_eventq* nimble_port_get_dflt_eventq() { return &queue; }
inline void ble_npl_eventq_put(ble_npl_eventq* target, ble_npl_event* event) {
  std::lock_guard<std::mutex> lock(target->mutex);
  target->events.push_back(event);
  target->ready.notify_all();
}
'''
PROGRAM = r'''
#include "BleHostOperation.h"
#include <atomic>
#include <cstdio>
bool NimBLEDevice::initialized = true;
ble_npl_eventq queue;
static std::thread::id hostId;
static std::atomic<bool> callbackActive{false};
static std::atomic<bool> operationDone{false};
static std::atomic<bool> callerReturned{false};
static std::atomic<bool> callerStarted{false};
static void* device;
int main() {
  NimBLEDevice::initialized = false;
  assert(!runBleHostOperation([](void*) { assert(false); }, nullptr));
  NimBLEDevice::initialized = true;
  assert(!runBleHostOperation(nullptr, nullptr));
  device = new int(42);
  StaticSemaphore_t entered, release;
  ble_npl_event gap;
  struct Context { StaticSemaphore_t* entered; StaticSemaphore_t* release; }
      context = {&entered, &release};
  ble_npl_event_init(&gap, [](ble_npl_event* event) {
    Context* context = static_cast<Context*>(event->argument);
    callbackActive = true;
    xSemaphoreGive(context->entered);
    xSemaphoreTake(context->release, portMAX_DELAY);
    // NimBLE still dereferences its result after user onResult() returns.
    assert(*static_cast<int*>(device) == 42);
    callbackActive = false;
  }, &context);
  ble_npl_eventq_put(&queue, &gap);
  std::thread host([] {
    hostId = std::this_thread::get_id();
    for (int i = 0; i < 2; ++i) {
      ble_npl_event* event;
      {
        std::unique_lock<std::mutex> lock(queue.mutex);
        queue.ready.wait(lock, [] { return !queue.events.empty(); });
        event = queue.events.front();
        queue.events.pop_front();
      }
      event->callback(event);
    }
  });
  xSemaphoreTake(&entered, portMAX_DELAY);
  std::thread caller([] {
    callerStarted = true;
    const bool ok = runBleHostOperation([](void* context) {
      assert(std::this_thread::get_id() == hostId);
      assert(!callbackActive);
      assert(context == device);
      delete static_cast<int*>(device);
      device = nullptr;
      operationDone = true;
    }, device);
    assert(ok && operationDone);
    callerReturned = true;
  });
  {
    std::unique_lock<std::mutex> lock(queue.mutex);
    queue.ready.wait(lock, [] { return !queue.events.empty(); });
  }
  assert(callerStarted && callbackActive && !operationDone && !callerReturned);
  xSemaphoreGive(&release);
  caller.join();
  host.join();
  assert(operationDone && callerReturned && device == nullptr);
  puts("BLE host serialization test passed");
}
'''

with tempfile.TemporaryDirectory(prefix="sharkdiv-ble-host-") as directory:
    temp = Path(directory)
    (temp / "stub.h").write_text(STUB)
    for name in ("NimBLEDevice.h", "freertos/FreeRTOS.h", "freertos/semphr.h",
                 "nimble/porting/nimble/include/nimble/nimble_port.h"):
        header = temp / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "stub.h"\n')
    (temp / "test.cpp").write_text(PROGRAM)
    subprocess.run([
        "clang++", "-std=c++11", "-pthread", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", f"-I{temp}", f"-I{ROOT / 'sharkdiv'}",
        str(temp / "test.cpp"), "-o", str(temp / "test"),
    ], check=True)
    subprocess.run([str(temp / "test")], check=True, timeout=10)
