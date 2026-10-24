#pragma once

#include <NimBLEDevice.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "nimble/nimble_port.h"
#else
#include "nimble/porting/nimble/include/nimble/nimble_port.h"
#endif

// Call only from the UI task, after BLE initialization, never from a BLE
// callback. The entire operation executes between NimBLE host events. A lock
// around application records cannot protect NimBLEScan's result vector: its
// event handler still reads advertisedDevice after onResult() returns.
//
// The completion wait deliberately has no timeout: abandoning a queued stack
// context would introduce a use-after-free, or a late start after feature exit.
// Operations must be short synchronous scan configuration/start/stop calls,
// never a blocking scan, delay, display operation, or wait for another BLE event.
inline bool runBleHostOperation(void (*operation)(void*), void* context) {
  if (!operation || !NimBLEDevice::getInitialized()) return false;
  struct Work {
    ble_npl_event event;
    StaticSemaphore_t completionStorage;
    SemaphoreHandle_t done;
    void (*operation)(void*);
    void* context;
  } work = {};
  // Caller-owned semaphore storage avoids heap failure during feature teardown.
  work.done = xSemaphoreCreateBinaryStatic(&work.completionStorage);
  work.operation = operation;
  work.context = context;
  ble_npl_event_init(&work.event, [](ble_npl_event* event) {
    Work* work = static_cast<Work*>(ble_npl_event_get_arg(event));
    work->operation(work->context);
    xSemaphoreGive(work->done);
  }, &work);
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &work.event);
  xSemaphoreTake(work.done, portMAX_DELAY);
  vSemaphoreDelete(work.done);
  return true;
}
