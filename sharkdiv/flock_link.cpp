// flock_link.cpp — see header. NimBLE GATT server with the FlockFinder
// service: detection + stream characteristics, JSON payloads.
#include "flock_link.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <cstdio>
#include <cstring>

namespace flock_link {

// FlockFinder app contract
static const char* kSvcUuid   = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
static const char* kDetUuid   = "beb5483e-36e1-4688-b7f5-ea07361b26a8";
static const char* kStreamUuid= "beb5483e-36e1-4688-b7f5-ea07361b26aa";

static BLEServer* s_server = nullptr;
static BLECharacteristic* s_det = nullptr;
static BLECharacteristic* s_str = nullptr;
static bool s_running = false;
static volatile bool s_connected = false;
static uint32_t s_tsBase = 0;

class LinkCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    s_connected = true;
    Serial.println("[flock-link] iOS app connected");
  }
  void onDisconnect(BLEServer*) override {
    s_connected = false;
    Serial.println("[flock-link] app disconnected");
    if (s_server) s_server->startAdvertising();
  }
};

static uint32_t ts() {
  if (!s_tsBase) s_tsBase = millis();
  return (millis() - s_tsBase) / 1000;
}

void start() {
  if (s_running) return;
  ensureBleStackReady();
  BLEDevice::setDeviceName("SHARKDIV");
  s_server = BLEDevice::createServer();
  s_server->setCallbacks(new LinkCb());
  BLEService* svc = s_server->createService(kSvcUuid);

  s_det = svc->createCharacteristic(
      kDetUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  s_det->setValue("{}");
  s_str = svc->createCharacteristic(
      kStreamUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  s_str->setValue("{}");

  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(kSvcUuid);
  adv->setScanResponse(true);
  s_server->getAdvertising()->start();
  s_running = true;
  Serial.println("[flock-link] GATT server up (FlockFinder)");
}

void stop() {
  if (!s_running) return;
  BLEDevice::getAdvertising()->stop();
  // connections close on their own when advertising stops
  s_running = false;
  s_connected = false;
  Serial.println("[flock-link] GATT server down");
}

bool active() { return s_running; }
bool clientConnected() { return s_connected; }

void pushDetection(const char* type, const char* mac, int rssi,
                   const char* confidence) {
  if (!s_running || !s_connected || !s_det) return;
  char json[160];
  snprintf(json, sizeof(json),
           "{\"type\":\"%s\",\"mac\":\"%s\",\"rssi\":%d,"
           "\"confidence\":\"%s\",\"ts\":%lu}",
           type, mac, rssi, confidence, (unsigned long)ts());
  s_det->setValue(json);
  s_det->notify();
  Serial.printf("[flock-link] det %s\n", json);
}

void pushStream(const char* line) {
  if (!s_running || !s_connected || !s_str) return;
  char buf[180];
  snprintf(buf, sizeof(buf), "{\"ts\":%lu,\"line\":\"%.150s\"}",
           (unsigned long)ts(), line);
  s_str->setValue(buf);
  s_str->notify();
}

}  // namespace flock_link
