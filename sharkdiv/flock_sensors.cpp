// flock_sensors.cpp — see header. Nothing here transmits; the nRF24 stays
// in RX (RPD carrier detect) and the CC1101 only reads RSSI.
#include "flock_sensors.h"
#include "flock_config.h"
#include <SPI.h>
#include "ELECHOUSE_CC1101_SRC_DRV.h"

namespace flock_sensors {

// ── nRF24: RPD carrier-detect sweep over a channel subset ──
#define FCE NRF24_SCAN_CE
#define FCSN NRF24_SCAN_CSN

static int s_nrfActivity = 0;
static int s_lteStrength = 0;
static bool s_lteBurst = false;
static bool s_nrfInit = false;

static byte rd(byte r) {
  byte c;
  digitalWrite(FCSN, LOW);
  SPI.transfer(r & 0x1F);
  c = SPI.transfer(0);
  digitalWrite(FCSN, HIGH);
  return c;
}
static void wr(byte r, byte v) {
  digitalWrite(FCSN, LOW);
  SPI.transfer((r & 0x1F) | 0x20);
  SPI.transfer(v);
  digitalWrite(FCSN, HIGH);
}

static void nrfInitOnce() {
  if (s_nrfInit) return;
  SPI.begin(NRF24_SPI_SCK, NRF24_SPI_MISO, NRF24_SPI_MOSI, NRF24_SPI_SS);
  SPI.setDataMode(SPI_MODE0);
  SPI.setFrequency(10000000);
  pinMode(FCE, OUTPUT);
  pinMode(FCSN, OUTPUT);
  digitalWrite(FCE, LOW);
  wr(0x00, rd(0x00) | 0x03);   // PWR_UP | PRIM_RX
  wr(0x01, 0x00);
  wr(0x06, 0x0F);
  s_nrfInit = true;
}

// Sweep 25 channels spread across the band, count carrier hits.
int sampleNrf() {
  nrfInitOnce();
  int hits = 0;
  for (int i = 0; i < 25; i++) {
    const uint8_t ch = i * 5;
    wr(0x05, ch);
    digitalWrite(FCE, HIGH);
    delayMicroseconds(160);
    digitalWrite(FCE, LOW);
    if (rd(0x09) & 0x01) hits++;   // RPD latched
  }
  s_nrfActivity = hits * 100 / flk::kNrfActiveHits;
  if (s_nrfActivity > 100) s_nrfActivity = 100;
  return s_nrfActivity;
}

// ── CC1101: RSSI-only burst sensing on LTE uplink spot frequencies ──
static void ccInitOnce() {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(270.0);
}

int sampleLte() {
  ccInitOnce();
  int strongest = -120;
  for (int i = 0; i < 3; i++) {
    ELECHOUSE_cc1101.SetRx(flk::kLteMHz[i]);   // SetRx(f) tunes + enters RX
    delay(2);
    const int dbm = ELECHOUSE_cc1101.getRssi();
    if (dbm > strongest) strongest = dbm;
  }
  s_lteBurst = strongest >= flk::kLteRssiDbm;
  if (s_lteBurst) {
    s_lteStrength = (strongest - flk::kLteRssiDbm) * 10;
    if (s_lteStrength > 100) s_lteStrength = 100;
  } else {
    s_lteStrength = 0;
  }
  return s_lteStrength;
}

// IR illuminator detection needs a raw photodiode on an ADC pin. This board
// carries only the TSOP demodulator, so the entry point reports unavailable.
bool sampleIr(int* adcOut) {
  if (adcOut) *adcOut = -1;
  return false;   // TSOP-only hardware
}

void begin() {
  s_nrfActivity = 0;
  s_lteStrength = 0;
  s_lteBurst = false;
}

bool lteBurstSeen() { return s_lteBurst; }
void poll() {}

}  // namespace flock_sensors
