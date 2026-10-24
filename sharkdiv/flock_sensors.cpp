// Supporting RF measurements only: neither receiver identifies camera hardware.
#include "flock_sensors.h"
#include "flock_config.h"
#include "utils.h"
#include <SPI.h>
#include "ELECHOUSE_CC1101_SRC_DRV.h"

namespace flock_sensors {
static int s_nrfActivity = -1, s_lteStrength = -1;
static bool s_lteBurst = false;
static byte rd(byte reg) {
  digitalWrite(NRF24_SCAN_CSN, LOW); SPI.transfer(reg & 0x1f);
  const byte value = SPI.transfer(0); digitalWrite(NRF24_SCAN_CSN, HIGH); return value;
}
static void wr(byte reg, byte value) {
  digitalWrite(NRF24_SCAN_CSN, LOW); SPI.transfer((reg & 0x1f) | 0x20);
  SPI.transfer(value); digitalWrite(NRF24_SCAN_CSN, HIGH);
}
int sampleNrf() {
  // SD and CC1101 may have changed the global SPI mapping since the last pass.
  reclaimSharedSpiBus();
  SPI.end();
  SPI.begin(NRF24_SPI_SCK, NRF24_SPI_MISO, NRF24_SPI_MOSI, NRF24_SPI_SS);
  pinMode(NRF24_SCAN_CE, OUTPUT); pinMode(NRF24_SCAN_CSN, OUTPUT);
  digitalWrite(NRF24_SCAN_CE, LOW); digitalWrite(NRF24_SCAN_CSN, HIGH);
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  wr(0x05, 37);
  if (rd(0x05) != 37) {
    SPI.endTransaction(); restoreSdAfterSharedSpi(); return s_nrfActivity = -1;
  }
  wr(0x00, 0x03); // PWR_UP, PRIM_RX; no transmit or auto-ack.
  wr(0x01, 0); wr(0x06, 0x06); // 1 Mbps, carrier mode disabled.
  delay(5);
  int hits = 0;
  for (int i = 0; i < 25; ++i) {
    wr(0x05, i * 5);
    digitalWrite(NRF24_SCAN_CE, HIGH);
    delayMicroseconds(180); // RX settle plus RPD dwell.
    digitalWrite(NRF24_SCAN_CE, LOW);
    if (rd(0x09) & 1) ++hits;
  }
  wr(0x00, 0x01); // Leave the receiver powered down between measurements.
  SPI.endTransaction();
  restoreSdAfterSharedSpi();
  return s_nrfActivity = hits * 100 / 25;
}
static bool ccPresent() {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CC1101_CS, LOW);
  const uint32_t start = micros();
  while (digitalRead(CC1101_MISO) && uint32_t(micros() - start) < 2000) {}
  byte version = 0xff;
  if (!digitalRead(CC1101_MISO)) { SPI.transfer(0xf1); version = SPI.transfer(0); }
  digitalWrite(CC1101_CS, HIGH); SPI.endTransaction();
  return version > 0 && version < 0xff;
}
int sampleLte() {
  // Historical API name; these are sub-GHz RSSI spots, not LTE identification.
  s_lteBurst = false;
  reclaimSharedSpiBus();
  SPI.end(); SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  pinMode(CC1101_CS, OUTPUT); digitalWrite(CC1101_CS, HIGH);
  if (!ccPresent()) { restoreSdAfterSharedSpi(); return s_lteStrength = -1; }
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0); ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(270.0);
  int strongest = -120;
  for (int i = 0; i < 3; ++i) {
    ELECHOUSE_cc1101.SetRx(flk::kLteMHz[i]);
    delay(3);
    const int dbm = ELECHOUSE_cc1101.getRssi();
    if (dbm > strongest) strongest = dbm;
  }
  ELECHOUSE_cc1101.setSidle();
  digitalWrite(CC1101_CS, HIGH);
  restoreSdAfterSharedSpi();
  s_lteBurst = strongest >= flk::kLteRssiDbm;
  return s_lteStrength = constrain((strongest + 110) * 100 / 70, 0, 100);
}
bool sampleIr(int* adcOut) { if (adcOut) *adcOut = -1; return false; }
void begin() { s_nrfActivity = s_lteStrength = -1; s_lteBurst = false; }
bool lteBurstSeen() { return s_lteBurst; }
void poll() {}
} // namespace flock_sensors
