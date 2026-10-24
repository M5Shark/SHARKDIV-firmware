// detectors.cpp — bounded, passive BLE/Wi-Fi broadcast candidate receivers.
// Match evidence and radio activity are shown separately from device identity.
#include "detectors.h"
#include "RemoteIdParser.h"
#include "BleHostOperation.h"
#include "DetectorSupport.h"
#include "FlockSignatures.h"
#include "PassiveWifiLifecycle.h"
#include <esp_netif.h>
#include "icon.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <cstring>
#include <cstdio>
#include "shared.h"

namespace Detectors {

// ── signatures ──────────────────────────────────────────────────────
struct Sig {
  const char* detName;         // detector title
  const char* category;        // shown for the hit
  const char* bleNames[4];     // BLE local-name substrings (nullptr end)
  uint16_t bleUuid16[4];       // advertised service UUIDs (0 = end)
  uint16_t bleCompany[2];      // manufacturer company IDs (0 = end)
  const char* wifiSsids[3];    // SSID substrings
  const char* wifiOuis[4];     // BSSID OUI prefixes "xx:xx:xx"
};

// per-detector icon + short chip label for the launcher
static const unsigned char* const kSigIcons[] = {
  bitmap_icon_analyzer,   // Flipper
  bitmap_icon_stat,       // Meshtastic
  bitmap_icon_stat,       // MeshCore
  bitmap_icon_tags,       // Tile
  bitmap_icon_tags,       // SmartTag
  bitmap_icon_dialog,     // RayBan
  bitmap_icon_list,       // iBeacon
  bitmap_icon_no_signal,  // Pineapple
  bitmap_icon_magnifying_glass, // Pwnagotchi
  bitmap_icon_scanner,    // Cameras
  bitmap_icon_kill,       // LE Gear  (placeholder reuse)
  bitmap_icon_satellite,  // Drone
  bitmap_icon_follow,     // Scout
};
static const char* const kSigChips[] = {
  "DEVICE", "MESH", "MESH", "TRACKER", "TRACKER", "WEARABLE",
  "LOCATION", "ROGUE AP", "AGENT", "CAMERA", "LE GEAR", "DRONE", "SWEEP",
};

static const Sig kSigs[] = {
  { "Flipper Zero", "multitool",
    {"Flipper", nullptr}, {}, {}, {}, {} },
  { "Meshtastic", "mesh node",
    {"Meshtastic", "meshtastic", nullptr}, {}, {0x0059}, {}, {} },
  { "MeshCore", "mesh node",
    {"MeshCore", "meshcore", nullptr}, {}, {}, {}, {} },
  { "Tile Tracker", "tracker",
    {}, {0xFEED}, {}, {}, {} },
  { "Samsung SmartTag", "tracker",
    {}, {}, {0x0075}, {}, {} },
  { "RayBan Meta", "smart glasses",
    {"Ray-Ban", "RayBan", "Stories", nullptr}, {}, {}, {}, {} },
  { "iBeacon", "location beacon",
    {}, {}, {0x004C}, {}, {} },   // + adv type 0x02 checked at runtime
  { "Pineapple", "rogue AP",
    {}, {}, {}, {"Pineapple", "pineapple", nullptr}, {} },
  { "Pwnagotchi", "wifi agent",
    {}, {}, {}, {"pwnagotchi", "Pwnagotchi", nullptr}, {} },
  { "WiFi Cameras", "camera",
    {}, {}, {}, {}, {} },          // Built-in Flock + optional custom camera OUIs.
  { "LE Gear", "law enforcement",
    {}, {}, {}, {}, {} },          // OUI list from SD (gear-ouis.txt)
  { "Drone RemoteID", "drone",
    {}, {}, {}, {}, {} },       // Dedicated validated Remote ID receiver below.
  { "Device Scout", "device",
    {nullptr}, {}, {}, {}, {} },   // ranks ALL BLE devices by persistence
};
static constexpr int kSigCount = sizeof(kSigs) / sizeof(kSigs[0]);

// Optional custom prefixes supplement the built-in Flock camera indicators.
static DetectorSupport::OuiList s_camOuis, s_gearOuis;
static void loadOuiFile(const char* path, DetectorSupport::OuiList& list) {
  list = DetectorSupport::OuiList{};
  File file = SD.open(path);
  if (!file) return;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    const int comment = line.indexOf('#');
    if (comment >= 0) line.remove(comment);
    line.trim();
    if (line.length()) list.add(line.c_str());
  }
  file.close();
}

using Hit = DetectorSupport::Hit;
static constexpr int kMaxHits = DetectorSupport::Table::capacity;
static DetectorSupport::Table s_table;
static portMUX_TYPE s_detectorLock = portMUX_INITIALIZER_UNLOCKED;
static const Sig* s_active = nullptr;
static bool s_inspectAll = false;
static uint32_t s_wifiFrames = 0, s_bleAdvertisements = 0;

static bool cameraMode(const Sig& s) { return !strcmp(s.detName, "WiFi Cameras"); }
static bool gearMode(const Sig& s) { return !strcmp(s.detName, "LE Gear"); }

static void upsert(const Sig* expected, const char* desc, const char* mac,
                   int8_t rssi, uint8_t channel, uint8_t evidence) {
  portENTER_CRITICAL(&s_detectorLock);
  if (s_active == expected)
    s_table.observe(desc, mac, rssi, channel, evidence, millis());
  portEXIT_CRITICAL(&s_detectorLock);
}
static int detectorSnapshot(Hit* out, uint32_t* wifiFrames = nullptr,
                            uint32_t* bleAdvertisements = nullptr) {
  portENTER_CRITICAL(&s_detectorLock);
  const int count = s_table.snapshot(out, kMaxHits, millis());
  if (wifiFrames) *wifiFrames = s_wifiFrames;
  if (bleAdvertisements) *bleAdvertisements = s_bleAdvertisements;
  portEXIT_CRITICAL(&s_detectorLock);
  return count;
}

static uint8_t bleMatch(const Sig& s, BLEAdvertisedDevice* device) {
  if (cameraMode(s))
    return FlockSignatures::bleAdvertisementEvidence(device->getPayload(),
                                                    device->getPayloadLength());
  uint8_t evidence = 0;
  if (device->haveName()) {
    const std::string name = device->getName();
    for (int i = 0; i < 4 && s.bleNames[i]; ++i)
      if (*s.bleNames[i] && name.find(s.bleNames[i]) != std::string::npos)
        evidence |= DetectorSupport::Name;
  }
  for (int i = 0; i < 4 && s.bleUuid16[i]; ++i) {
    const NimBLEUUID uuid(s.bleUuid16[i]);
    if (device->isAdvertisingService(uuid)) evidence |= DetectorSupport::Service;
    for (uint8_t n = 0; n < device->getServiceDataCount(); ++n)
      if (device->getServiceDataUUID(n) == uuid) evidence |= DetectorSupport::Service;
  }
  for (uint8_t n = 0; n < device->getManufacturerDataCount(); ++n) {
    const std::string data = device->getManufacturerData(n);
    if (data.size() < 2) continue;
    const uint16_t company = static_cast<uint8_t>(data[0]) |
                             (static_cast<uint8_t>(data[1]) << 8);
    if (!strcmp(s.detName, "iBeacon")) {
      // Apple's company ID alone matches many devices, not just iBeacons.
      if (data.size() >= 25 && company == 0x004c &&
          data[2] == 0x02 && data[3] == 0x15) evidence |= DetectorSupport::Service;
    } else {
      for (int i = 0; i < 2 && s.bleCompany[i]; ++i)
        if (company == s.bleCompany[i]) evidence |= DetectorSupport::Vendor;
    }
  }
  return evidence;
}

class DetCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* device) override {
    if (!device) return;
    portENTER_CRITICAL(&s_detectorLock);
    const Sig* active = s_active;
    const bool inspect = s_inspectAll;
    if (active && s_bleAdvertisements != UINT32_MAX) ++s_bleAdvertisements;
    portEXIT_CRITICAL(&s_detectorLock);
    if (!active) return;
    uint8_t evidence = bleMatch(*active, device);
    if (!evidence && (inspect || !strcmp(active->detName, "Device Scout")))
      evidence = DetectorSupport::Advertisement;
    if (!evidence) return;
    std::string name = device->haveName() ? device->getName() :
                       cameraMode(*active) ? "Flock BLE candidate" : "Unnamed BLE";
    // Advertised names are untrusted display text.
    for (char& c : name) if (static_cast<uint8_t>(c) < 32 || static_cast<uint8_t>(c) > 126) c = '?';
    upsert(active, name.c_str(), device->getAddress().toString().c_str(),
           device->getRSSI(), 0, evidence);
  }
};
static DetCb s_cb;

static void detectorWifiCallback(void* buffer, wifi_promiscuous_pkt_type_t type) {
  if (!buffer || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet = static_cast<const wifi_promiscuous_pkt_t*>(buffer);
  if (packet->rx_ctrl.sig_len < 28) return;
  DetectorSupport::WifiAdvertisement advertisement;
  if (!DetectorSupport::parseWifiAdvertisement(packet->payload,
        packet->rx_ctrl.sig_len - 4, advertisement)) return;
  portENTER_CRITICAL(&s_detectorLock);
  const Sig* active = s_active;
  if (active && s_wifiFrames != UINT32_MAX) ++s_wifiFrames;
  portEXIT_CRITICAL(&s_detectorLock);
  if (!active) return;
  uint8_t evidence = 0;
  if (cameraMode(*active)) {
    const auto probe = FlockSignatures::probeFrame(packet->payload,
                                                   packet->rx_ctrl.sig_len, true);
    if (probe.valid && probe.fingerprint) evidence |= DetectorSupport::Fingerprint;
    if (FlockSignatures::wifiOui(advertisement.mac) || s_camOuis.matches(advertisement.mac))
      evidence |= DetectorSupport::Vendor;
    if (FlockSignatures::ssid(advertisement.ssid)) evidence |= DetectorSupport::Name;
    if ((evidence & DetectorSupport::Vendor) && advertisement.wildcard)
      evidence |= DetectorSupport::Probe;
  } else if (gearMode(*active)) {
    if (s_gearOuis.matches(advertisement.mac)) evidence |= DetectorSupport::Vendor;
  } else {
    for (int i = 0; i < 3 && active->wifiSsids[i]; ++i)
      if (strstr(advertisement.ssid, active->wifiSsids[i])) evidence |= DetectorSupport::Name;
  }
  if (!evidence) return;
  char mac[18];
  snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x",
           advertisement.mac[0], advertisement.mac[1], advertisement.mac[2],
           advertisement.mac[3], advertisement.mac[4], advertisement.mac[5]);
  const char* label = advertisement.ssid[0] ? advertisement.ssid :
      advertisement.probe ? "Probe transmitter" : "Hidden network";
  upsert(active, label, mac, packet->rx_ctrl.rssi, packet->rx_ctrl.channel, evidence);
}

struct DetectorWifiDriver {
  bool arduinoStation() { return WiFi.mode(WIFI_MODE_STA); }
  void disconnectStation() { WiFi.disconnect(false, false); }
  void cancelScan() { esp_wifi_scan_stop(); WiFi.scanDelete(); }
  void disableMonitor() { esp_wifi_set_promiscuous(false); }
  bool nullMode() { return esp_wifi_set_mode(WIFI_MODE_NULL) == ESP_OK; }
  bool startDriver() { return esp_wifi_start() == ESP_OK; }
  void detachReceiver() { esp_wifi_set_promiscuous_rx_cb(nullptr); }
  bool initialized() {
    wifi_mode_t mode;
    return esp_wifi_get_mode(&mode) != ESP_ERR_WIFI_NOT_INIT;
  }
  bool stationMode() { return esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK; }
  bool arduinoOff() { return WiFi.mode(WIFI_OFF); }
};
static bool stopDetectorWifi() {
  DetectorWifiDriver radio;
  return PassiveWifiLifecycle::stop(radio);
}
static bool startDetectorWifi() {
  DetectorWifiDriver radio;
  if (!PassiveWifiLifecycle::start(radio)) return false;
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  return esp_wifi_set_promiscuous_filter(&filter) == ESP_OK &&
         esp_wifi_set_promiscuous_rx_cb(&detectorWifiCallback) == ESP_OK &&
         esp_wifi_set_promiscuous(true) == ESP_OK &&
         esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

// ── detector screens ───────────────────────────────────────────────
static void chrome(const char* title, const char* chip) {
  (void)chip;
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("Prev", "Clear", "Exit", "View", "Next");
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.fillRect(0, 22, tft.width(), 23, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.drawString(title, 8, 26, 2);
  tft.drawFastHLine(8, 44, tft.width() - 16, UI_LINE);
  setupTouchscreen();
  redrawTouchButtonBar();
}

static void paintHits(int& cursor, bool details, const Sig& signature,
                      bool wifiWanted, bool bleWanted, bool wifiOk, bool bleOk,
                      uint8_t channel, bool inspector) {
  Hit hits[kMaxHits];
  uint32_t wifiFrames = 0, bleAdvertisements = 0;
  const int count = detectorSnapshot(hits, &wifiFrames, &bleAdvertisements);
  if (cursor >= count) cursor = 0;
  const int bottom = touchNavContentBottomY() > 0 ? touchNavContentBottomY()
                                                 : tft.height() - 60;
  tft.fillRect(0, 46, tft.width(), bottom - 46, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  char text[80];
  snprintf(text, sizeof(text), "WiFi %s  BLE %s  CH %02u",
      !wifiWanted ? "--" : wifiOk ? "ON" : "ERR",
      !bleWanted ? "--" : bleOk ? "ON" : "ERR", channel);
  tft.drawString(text, 8, 50, 1);
  snprintf(text, sizeof(text), "RX frames %lu  BLE ads %lu",
      static_cast<unsigned long>(wifiFrames), static_cast<unsigned long>(bleAdvertisements));
  tft.drawString(text, 8, 64, 1);
  snprintf(text, sizeof(text), "%u %s / %s", count, inspector ? "devices" : "candidates",
      details ? "DETAIL" : "LIST");
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(text, 8, 78, 1);
  if (!count) {
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.drawString(gearMode(signature) && !s_gearOuis.count ? "No filter configured" :
                   "No matches received", 8, 106, 2);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    if (gearMode(signature) && !s_gearOuis.count) {
      tft.drawString("No custom prefixes loaded.", 8, 135, 1);
      tft.drawString("Add xx:xx:xx lines to SD:", 8, 151, 1);
      tft.drawString("/flock/gear-ouis.txt", 8, 167, 1);
    } else if (cameraMode(signature)) {
      snprintf(text, sizeof(text), "Flock indicators + %u custom OUIs", s_camOuis.count);
      tft.drawString(text, 8, 135, 1);
      tft.drawString("WiFi probe + beacon / BLE ads", 8, 151, 1);
      tft.drawString("Built-in profiles need no SD card.", 8, 167, 1);
      tft.drawString("No result does not rule out cameras.", 8, 189, 1);
    } else {
      tft.drawString("Listening for matching broadcasts.", 8, 135, 1);
      tft.drawString("RX counters show radio activity.", 8, 155, 1);
    }
  } else if (details) {
    const Hit& hit = hits[cursor];
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    snprintf(text, sizeof(text), "%u/%u %.30s", cursor + 1, count, hit.desc);
    tft.drawString(text, 8, 103, 1);
    tft.drawString(hit.mac, 8, 122, 1);
    snprintf(text, sizeof(text), "%s  %d dBm  CH %u", hit.channel ? "WIFI" : "BLE", hit.rssi, hit.channel);
    tft.drawString(text, 8, 141, 1);
    snprintf(text, sizeof(text), "Match:%s%s%s%s%s%s",
        hit.evidence & DetectorSupport::Name ? " name" : "",
        hit.evidence & DetectorSupport::Vendor ? " vendor" : "",
        hit.evidence & DetectorSupport::Service ? " service" : "",
        hit.evidence & DetectorSupport::Probe ? " probe" : "",
        hit.evidence & DetectorSupport::Advertisement ? " advert" : "",
        hit.evidence & DetectorSupport::Fingerprint ? " fingerprint" : "");
    if (strlen(text) > 36) {
      char second[40];
      snprintf(second, sizeof(second), "%s", text + 36);
      text[36] = 0;
      tft.drawString(second, 8, 176, 1);
    }
    tft.drawString(text, 8, 162, 1);
    snprintf(text, sizeof(text), "Seen %u times / last %lus ago", hit.seen,
        static_cast<unsigned long>((millis() - hit.lastMs) / 1000));
    tft.drawString(text, 8, 192, 1);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("RSSI is signal, not distance.", 8, 209, 1);
  } else {
    for (int i = (cursor / 4) * 4; i < count && i < (cursor / 4 + 1) * 4; ++i) {
      const int y = 100 + (i % 4) * 31;
      tft.setTextColor(i == cursor ? UI_ICON : UI_TEXT, TFT_BLACK);
      snprintf(text, sizeof(text), "%c %.32s", i == cursor ? '>' : ' ', hits[i].desc);
      tft.drawString(text, 8, y, 1);
      tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
      snprintf(text, sizeof(text), "  %s  %d dBm  %lus ago", hits[i].channel ? "WIFI" : "BLE", hits[i].rssi,
          static_cast<unsigned long>((millis() - hits[i].lastMs) / 1000));
      tft.drawString(text, 8, y + 13, 1);
    }
  }
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("Indicators only / identity unverified", 8, bottom - 14, 1);
}

// Remote ID gets its own bounded table: arbitrary manufacturers and SSIDs
// never count as drones. Callback tasks only decode and copy; UI draws snapshots.
struct RidHit {
  uint8_t mac[6];
  uint8_t channel;  // 0 = BLE, otherwise 2.4 GHz Wi-Fi channel.
  int8_t rssi;
  RemoteId::Observation value;
  uint32_t seenMs, idMs, locationMs;
};
static constexpr int kRidMax = 12;
static RidHit s_ridHits[kRidMax];
static int s_ridCount = 0;
static bool s_ridReceiving = false;  // Always accessed under s_ridLock.
static portMUX_TYPE s_ridLock = portMUX_INITIALIZER_UNLOCKED;

static void ridRecord(const uint8_t* mac, uint8_t channel, int8_t rssi,
                      const RemoteId::Observation& value) {
  portENTER_CRITICAL(&s_ridLock);
  if (!s_ridReceiving) { portEXIT_CRITICAL(&s_ridLock); return; }
  const uint32_t now = millis();
  int slot = -1;
  for (int i = 0; i < s_ridCount; ++i) {
    if (!memcmp(s_ridHits[i].mac, mac, 6) &&
        (s_ridHits[i].channel == 0) == (channel == 0)) { slot = i; break; }
  }
  if (slot < 0) {
    if (s_ridCount < kRidMax) slot = s_ridCount++;
    else {
      slot = 0;
      for (int i = 1; i < kRidMax; ++i)
        if (now - s_ridHits[i].seenMs > now - s_ridHits[slot].seenMs) slot = i;
    }
    s_ridHits[slot] = RidHit{};
    memcpy(s_ridHits[slot].mac, mac, 6);
  }
  RidHit& hit = s_ridHits[slot];
  hit.channel = channel;
  hit.rssi = rssi;
  hit.seenMs = now;
  hit.value.version = value.version;
  if (value.hasId) {
    hit.value.hasId = true;
    hit.value.idType = value.idType;
    hit.value.aircraftType = value.aircraftType;
    memcpy(hit.value.id, value.id, sizeof(value.id));
    hit.idMs = now;
  }
  if (value.hasLocation) {
    hit.value.hasLocation = true;
    hit.value.positionValid = value.positionValid;
    hit.value.altitudeValid = value.altitudeValid;
    hit.value.speedValid = value.speedValid;
    hit.value.status = value.status;
    hit.value.latitude = value.latitude;
    hit.value.longitude = value.longitude;
    hit.value.altitude = value.altitude;
    hit.value.speed = value.speed;
    hit.locationMs = now;
  }
  portEXIT_CRITICAL(&s_ridLock);
}

class RidCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* device) override {
    if (!device) return;
    RemoteId::Observation value;
    if (!RemoteId::parseBleAdvertisement(device->getPayload(),
                                         device->getPayloadLength(), value)) return;
    unsigned int b[6];
    const std::string address = device->getAddress().toString();
    if (sscanf(address.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return;
    uint8_t mac[6];
    for (int i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(b[i]);
    ridRecord(mac, 0, device->getRSSI(), value);
  }
};
static RidCallbacks s_ridCallbacks;

static void ridWifiCallback(void* buffer, wifi_promiscuous_pkt_type_t type) {
  if (!buffer || type != WIFI_PKT_MGMT) return;
  const wifi_promiscuous_pkt_t* packet =
      static_cast<const wifi_promiscuous_pkt_t*>(buffer);
  // ESP-IDF sig_len includes the four-byte FCS. It is not an information element.
  const size_t length = packet->rx_ctrl.sig_len;
  if (length < 40) return;
  RemoteId::Observation value;
  if (RemoteId::parseWifiBeacon(packet->payload, length - 4, value))
    ridRecord(packet->payload + 10, packet->rx_ctrl.channel,
              packet->rx_ctrl.rssi, value);
}

static bool ridStartWifi() {
  DetectorWifiDriver radio;
  if (!PassiveWifiLifecycle::start(radio)) return false;
  wifi_promiscuous_filter_t filter = {};
  filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  return esp_wifi_set_promiscuous_filter(&filter) == ESP_OK &&
         esp_wifi_set_promiscuous_rx_cb(&ridWifiCallback) == ESP_OK &&
         esp_wifi_set_promiscuous(true) == ESP_OK &&
         esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

static int ridSnapshot(RidHit* out) {
  portENTER_CRITICAL(&s_ridLock);
  const uint32_t now = millis();
  int count = 0;
  for (int i = 0; i < s_ridCount; ++i) {
    if (now - s_ridHits[i].seenMs <= 30000) {
      RidHit& hit = s_ridHits[i];
      if (now - hit.idMs > 30000) hit.value.hasId = false;
      if (now - hit.locationMs > 10000) hit.value.hasLocation = false;
      s_ridHits[count++] = hit;
    }
  }
  s_ridCount = count;
  memcpy(out, s_ridHits, count * sizeof(RidHit));
  portEXIT_CRITICAL(&s_ridLock);
  return count;
}

static const char* ridStatus(uint8_t status) {
  switch (status) {
    case 1: return "GROUND";
    case 2: return "AIRBORNE";
    case 3: return "EMERGENCY";
    case 4: return "RID FAULT";
    default: return "UNDECLARED";
  }
}

static void ridPaint(int& cursor, bool details, bool wifiOk, bool bleOk,
                     uint8_t channel) {
  RidHit hits[kRidMax];
  const int count = ridSnapshot(hits);
  if (cursor >= count) cursor = 0;
  const int bottom = touchNavContentBottomY() > 0 ? touchNavContentBottomY()
                                                 : tft.height() - 60;
  tft.fillRect(0, 46, tft.width(), bottom - 46, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  char text[64];
  snprintf(text, sizeof(text), "RX  BLE:%s  WiFi:%s  CH %02u",
           bleOk ? "ON" : "ERR", wifiOk ? "ON" : "ERR", channel);
  tft.drawString(text, 8, 50, 1);
  snprintf(text, sizeof(text), "%u broadcasts  /  %s", count, details ? "DETAIL" : "LIST");
  tft.drawString(text, 8, 66, 1);
  tft.drawFastHLine(8, 80, tft.width() - 16, UI_LINE);
  if (!count) {
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.drawString("Listening for Remote ID", 8, 99, 2);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("BLE legacy + WiFi beacons", 8, 124, 1);
    tft.drawString("2.4 GHz channels 1-11", 8, 140, 1);
    tft.drawString("No signal is not proof of no drone.", 8, 164, 1);
  } else if (!details) {
    const int page = cursor / 4;
    for (int i = page * 4; i < count && i < (page + 1) * 4; ++i) {
      const int y = 88 + (i % 4) * 35;
      tft.setTextColor(i == cursor ? UI_ICON : UI_TEXT, TFT_BLACK);
      snprintf(text, sizeof(text), "%c %.20s", i == cursor ? '>' : ' ',
               hits[i].value.hasId ? hits[i].value.id : "Awaiting Basic ID");
      tft.drawString(text, 8, y, 1);
      snprintf(text, sizeof(text), "  %s  %d dBm  %lus ago",
               hits[i].channel ? "WIFI" : "BLE", hits[i].rssi,
               static_cast<unsigned long>((millis() - hits[i].seenMs) / 1000));
      tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
      tft.drawString(text, 8, y + 14, 1);
    }
  } else {
    const RidHit& hit = hits[cursor];
    const RemoteId::Observation& value = hit.value;
    snprintf(text, sizeof(text), "%u/%u  %s  %d dBm  v%u", cursor + 1, count,
             hit.channel ? "WIFI" : "BLE", hit.rssi, value.version);
    tft.setTextColor(UI_ICON, TFT_BLACK);
    tft.drawString(text, 8, 88, 1);
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    snprintf(text, sizeof(text), "%.20s", value.hasId ? value.id : "Awaiting Basic ID");
    tft.drawString(text, 8, 104, 1);
    if (value.hasId && strlen(value.id) > 20)
      tft.drawString(value.id + 20, 8, 116, 1);
    snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X  %lus",
             hit.mac[0], hit.mac[1], hit.mac[2], hit.mac[3], hit.mac[4], hit.mac[5],
             static_cast<unsigned long>((millis() - hit.seenMs) / 1000));
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString(text, 8, 132, 1);
    if (value.hasLocation) {
      if (value.positionValid) {
        snprintf(text, sizeof(text), "LAT  %.6f", value.latitude);
        tft.drawString(text, 8, 150, 1);
        snprintf(text, sizeof(text), "LON  %.6f", value.longitude);
        tft.drawString(text, 8, 166, 1);
      } else tft.drawString("Position not broadcast", 8, 150, 1);
      if (value.altitudeValid) snprintf(text, sizeof(text), "ALT  %.1f m WGS84", value.altitude);
      else snprintf(text, sizeof(text), "ALT  unavailable");
      tft.drawString(text, 8, 182, 1);
      if (value.speedValid) snprintf(text, sizeof(text), "SPD  %.2f m/s", value.speed);
      else snprintf(text, sizeof(text), "SPD  unavailable");
      tft.drawString(text, 8, 198, 1);
      snprintf(text, sizeof(text), "%s  LOC %lus old", ridStatus(value.status),
               static_cast<unsigned long>((millis() - hit.locationMs) / 1000));
      tft.drawString(text, 8, 216, 1);
    } else tft.drawString("Location pending / expired", 8, 150, 1);
  }
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("Broadcast data / unverified", 8, bottom - 14, 1);
}

static void runRemoteId() {
  pauseBackgroundRadioTasks();
  portENTER_CRITICAL(&s_detectorLock);
  s_active = nullptr;
  portEXIT_CRITICAL(&s_detectorLock);
  portENTER_CRITICAL(&s_ridLock);
  s_ridCount = 0;
  s_ridReceiving = true;
  portEXIT_CRITICAL(&s_ridLock);
  chrome("REMOTE ID", nullptr);
  setTouchNavLabels("Prev", "Clear", "Exit", "View", "Next");
  redrawTouchButtonBar();
  const bool wifiOk = ridStartWifi();
  const bool bleReady = ensureBleStackReady();
  struct ScanStart {
    BLEScan* scan;
    bool configured;
    bool started;
  } start = {nullptr, false, false};
  const bool bleScheduled = bleReady && runBleHostOperation([](void* context) {
    ScanStart& request = *static_cast<ScanStart*>(context);
    request.scan = BLEDevice::getScan();
    // A failed cancellation can leave the controller scanning. Do not replace
    // its callbacks or clear results until cancellation actually succeeds.
    if (!request.scan || !request.scan->stop()) return;
    BLEDevice::getAdvertising()->stop();
    request.scan->clearResults();
    request.scan->setAdvertisedDeviceCallbacks(&s_ridCallbacks, true);
    request.scan->setActiveScan(false);
    request.scan->setInterval(160);
    request.scan->setWindow(80);
    request.scan->setDuplicateFilter(false); // Receive refreshed Location.
    request.scan->setMaxResults(0);          // Callback-only bounded storage.
    request.configured = true;
    request.started = request.scan->start(0, nullptr, false);
  }, &start);
  const bool bleOk = bleScheduled && start.started;
  if (!bleOk) Serial.println("[RemoteID] BLE unavailable or scan cancellation/start failed");
  int cursor = 0;
  bool details = true;
  uint8_t channel = 6;
  uint32_t lastHop = millis(), lastPaint = 0;
  while (!feature_exit_requested) {
    if (featureExitButtonPressed()) break;
    if (isButtonPressedEdge(BTN_LEFT)) {
      cursor = cursor ? cursor - 1 : 0;
    } else if (isButtonPressedEdge(BTN_RIGHT)) {
      cursor++;
    } else if (isButtonPressedEdge(BTN_UP)) {
      details = !details;
    } else if (isButtonPressedEdge(BTN_DOWN)) {
      portENTER_CRITICAL(&s_ridLock);
      s_ridCount = 0;
      portEXIT_CRITICAL(&s_ridLock);
      cursor = 0;
    }
    const uint32_t now = millis();
    if (wifiOk && now - lastHop >= 300) {
      lastHop = now;
      channel = channel % 11 + 1;
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }
    if (now - lastPaint >= 350) {
      lastPaint = now;
      ridPaint(cursor, details, wifiOk, bleOk, channel);
    }
    delay(20);
  }
  portENTER_CRITICAL(&s_ridLock);
  s_ridReceiving = false;
  portEXIT_CRITICAL(&s_ridLock);
  if (start.configured) {
    struct ScanStop { BLEScan* scan; bool stopped; } stop = {start.scan, false};
    const bool executed = runBleHostOperation([](void* context) {
      ScanStop& request = *static_cast<ScanStop*>(context);
      if (!request.scan->stop()) return;
      request.scan->setAdvertisedDeviceCallbacks(nullptr);
      request.scan->clearResults();
      request.scan->setMaxResults(0xff); // NimBLE 1.4 default only after stop.
      request.stopped = true;
    }, &stop);
    if (!executed || !stop.stopped) {
      // The static callback remains valid and writes are disabled above. Keep
      // maxResults(0) so a failed stop cannot grow the library's result vector.
      Serial.println("[RemoteID] BLE stop failed; bounded scan retained, restart recommended");
    }
  }
  if (!stopDetectorWifi()) Serial.println("[RemoteID] WiFi shutdown failed");
}

static void runDetector(int sigIdx, bool inspector = false) {
  const Sig& signature = kSigs[sigIdx];
  if (!strcmp(signature.detName, "Drone RemoteID")) { runRemoteId(); return; }
  pauseBackgroundRadioTasks();
  portENTER_CRITICAL(&s_detectorLock);
  s_active = &signature;
  s_inspectAll = inspector;
  s_table.clear();
  s_wifiFrames = s_bleAdvertisements = 0;
  portEXIT_CRITICAL(&s_detectorLock);
  chrome(inspector ? "BLE Inspector" : signature.detName, nullptr);
  const bool wantWifi = cameraMode(signature) ||
      (gearMode(signature) && s_gearOuis.count > 0) || signature.wifiSsids[0];
  const bool wantBle = cameraMode(signature) || signature.bleNames[0] ||
      signature.bleUuid16[0] || signature.bleCompany[0] || inspector ||
      !strcmp(signature.detName, "Device Scout");
  const bool wifiOk = wantWifi && startDetectorWifi();
  struct BleStart { BLEScan* scan; bool configured; bool started; } ble = {nullptr, false, false};
  const bool bleExecuted = wantBle && ensureBleStackReady() && runBleHostOperation([](void* context) {
    BleStart& request = *static_cast<BleStart*>(context);
    request.scan = BLEDevice::getScan();
    if (!request.scan || !request.scan->stop()) return;
    BLEDevice::getAdvertising()->stop();
    request.scan->clearResults();
    request.scan->setAdvertisedDeviceCallbacks(&s_cb, true);
    request.scan->setActiveScan(false);
    request.scan->setInterval(160);
    request.scan->setWindow(80);
    request.scan->setDuplicateFilter(false);
    request.scan->setMaxResults(0);
    request.configured = true;
    request.started = request.scan->start(0, nullptr, false);
  }, &ble);
  const bool bleOk = bleExecuted && ble.started;
  if ((wantWifi && !wifiOk) || (wantBle && !bleOk))
    Serial.printf("[detectors] radio error: WiFi %d BLE %d\n", wifiOk, bleOk);
  int cursor = 0;
  bool details = false;
  uint8_t channel = 6;
  uint32_t lastHop = millis(), lastPaint = 0;
  while (!feature_exit_requested) {
    if (featureExitButtonPressed()) break;
    if (isButtonPressedEdge(BTN_LEFT)) cursor = cursor > 0 ? cursor - 1 : 0;
    else if (isButtonPressedEdge(BTN_RIGHT)) ++cursor;
    else if (isButtonPressedEdge(BTN_UP)) details = !details;
    else if (isButtonPressedEdge(BTN_DOWN)) {
      portENTER_CRITICAL(&s_detectorLock);
      s_table.clear();
      s_wifiFrames = s_bleAdvertisements = 0;
      portEXIT_CRITICAL(&s_detectorLock);
      cursor = 0;
    }
    const uint32_t now = millis();
    if (wifiOk && now - lastHop >= 250) {
      lastHop = now;
      channel = channel % 11 + 1;
      esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }
    if (now - lastPaint >= 350) {
      lastPaint = now;
      paintHits(cursor, details, signature, wantWifi, wantBle, wifiOk, bleOk, channel, inspector);
    }
    delay(20);
  }
  portENTER_CRITICAL(&s_detectorLock);
  s_active = nullptr;
  s_inspectAll = false;
  portEXIT_CRITICAL(&s_detectorLock);
  if (ble.configured) {
    struct BleStop { BLEScan* scan; bool stopped; } stop = {ble.scan, false};
    const bool executed = runBleHostOperation([](void* context) {
      BleStop& request = *static_cast<BleStop*>(context);
      if (!request.scan->stop()) return;
      request.scan->setAdvertisedDeviceCallbacks(nullptr);
      request.scan->clearResults();
      request.scan->setMaxResults(0xff);
      request.stopped = true;
    }, &stop);
    if (!executed || !stop.stopped)
      Serial.println("[detectors] BLE stop failed; bounded scan retained, restart recommended");
  }
  if (wantWifi) {
    if (!stopDetectorWifi()) Serial.println("[detectors] WiFi shutdown failed");
  }
}

static void waitDetectorRelease() {
  int x, y;
  while (readTouchXYDismiss(x, y) || isButtonPressed(BTN_SELECT)) delay(10);
  delay(360); // Drain the global feature-exit tap latch before changing screens.
}

void runLauncher() {
  loadOuiFile("/flock/camera-ouis.txt", s_camOuis);
  loadOuiFile("/flock/gear-ouis.txt", s_gearOuis);
  const int listCount = kSigCount + 1;
  int cursor = 0;
  bool redraw = true;
  feature_active = true;
  feature_exit_requested = false;
  const int top = 52;
  const int rowHeight = 37;
  const int bottom = touchNavContentBottomY() > 0 ? touchNavContentBottomY()
                                                 : tft.height() - 60;
  const int rows = max(1, (bottom - top - 18) / rowHeight);
  while (!feature_exit_requested) {
    const int first = (cursor / rows) * rows;
    if (redraw) {
      redraw = false;
      chrome("DETECTORS", nullptr);
      setTouchNavLabels("Back", "Down", "Open", "Up", "Page");
      redrawTouchButtonBar();
      tft.fillRect(0, 46, tft.width(), bottom - 46, TFT_BLACK);
      tft.setTextDatum(TL_DATUM);
      for (int row = 0; row < rows && first + row < listCount; ++row) {
        const int index = first + row;
        const int y = top + row * rowHeight;
        const bool selected = index == cursor;
        const uint16_t background = selected ? UI_ICON : TFT_BLACK;
        tft.fillRect(8, y, tft.width() - 16, rowHeight - 2, background);
        tft.setTextColor(selected ? TFT_BLACK : UI_TEXT, background);
        tft.drawString(index < kSigCount ? kSigs[index].detName : "BLE Inspector", 14, y + 3, 2);
        tft.setTextColor(selected ? TFT_BLACK : UI_DIM_TEXT, background);
        tft.drawString(index < kSigCount ? kSigChips[index] : "ADVERTISEMENT VIEW", 14, y + 22, 1);
      }
      char page[32];
      snprintf(page, sizeof(page), "Page %u/%u  /  %u tools", cursor / rows + 1,
               (listCount + rows - 1) / rows, listCount);
      tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
      tft.drawString(page, 8, bottom - 14, 1);
    }
    if (isButtonPressedEdge(BTN_LEFT)) break;
    bool open = false;
    if (isButtonPressedEdge(BTN_UP)) {
      cursor = (cursor + listCount - 1) % listCount;
      redraw = true;
    } else if (isButtonPressedEdge(BTN_DOWN)) {
      cursor = (cursor + 1) % listCount;
      redraw = true;
    } else if (isButtonPressedEdge(BTN_RIGHT)) {
      cursor = first + rows < listCount ? first + rows : 0;
      redraw = true;
    } else if (isButtonPressedEdge(BTN_SELECT)) open = true;
    int x, y;
    if (!open && readTouchXY(x, y) && x >= 8 && x < tft.width() - 8 &&
        y >= top && y < top + rows * rowHeight) {
      const int index = first + (y - top) / rowHeight;
      if (index < listCount) { cursor = index; open = true; }
    }
    if (open) {
      waitDetectorRelease();
      if (cursor < kSigCount) runDetector(cursor);
      else runDetector(kSigCount - 1, true);
      waitDetectorRelease();
      feature_active = true;
      feature_exit_requested = false;
      redraw = true;
    }
    delay(15);
  }
  waitDetectorRelease();
  setTouchButtonInputEnabled(false);
  feature_active = false;
  feature_exit_requested = false;
  is_main_menu = false;
}

} // namespace Detectors
