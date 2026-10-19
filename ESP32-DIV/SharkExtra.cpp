// SharkExtra.cpp — QR Code tool + WiFi Channel Analyzer.
// Both are original implementations on the ESP32-DIV feature contract; QR
// encoding is the MIT-licensed QRCode C library vendored in this sketch.

#include "SharkExtra.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"
#include <WiFi.h>
#include "esp_wifi.h"
#include "qrcode.h"

extern volatile bool serialConsoleEnabled;

namespace {

void extraChrome(const char* title) {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);
  if (featureHasTouchNavBar()) {
    featureClearContent(TFT_BLACK);
  } else {
    tft.fillScreen(TFT_BLACK);
  }
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(String("// ") + title, 6, 28, 2);
  tft.drawFastHLine(0, 38, tft.width(), UI_LINE);
  tft.setTextDatum(TL_DATUM);
  setupTouchscreen();
  redrawTouchButtonBar();
}

bool extraExitRequested() {
  maintainTouchNavBar();
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return true;
  }
  return false;
}

}  // namespace

// ──────────────────────────────────────────────────────────── QR Code ──
// Renders scannable QR codes on the display: pick a preset with UP/DOWN, or
// feed custom text over the serial console (end with Enter).
namespace QrTool {

static const char* const kPresets[] = {
  "M5SHARK shop",
  "SHARKDIV updates",
  "Device info",
  "Serial input",
};
static constexpr int kPresetCount = 4;

static int      s_preset = 0;
static char     s_text[240] = "https://m5shark.com";
static char     s_serial[240] = {0};
static int      s_serialLen = 0;
static uint32_t s_lastBtnMs = 0;
static constexpr uint32_t kBtnDebounce = 250;

static void applyPreset() {
  switch (s_preset) {
    case 0: strncpy(s_text, "https://m5shark.com", sizeof(s_text) - 1); break;
    case 1: strncpy(s_text,
                    "https://github.com/M5Shark/SHARKDIV-updates",
                    sizeof(s_text) - 1); break;
    case 2: {
      snprintf(s_text, sizeof(s_text),
               "SHARKDIV %s | ESP32-S3 | WiFi %s",
               ESP32DIV_VERSION, WiFi.macAddress().c_str());
      break;
    }
    case 3:
      s_text[0] = 0;
      if (s_serialLen > 0) {
        strncpy(s_text, s_serial, sizeof(s_text) - 1);
      }
      break;
  }
  s_text[sizeof(s_text) - 1] = 0;
}

static void renderQr() {
  tft.fillRect(0, 42, tft.width(), 230, TFT_BLACK);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  String label = String(kPresets[s_preset]);
  if (s_preset == 3) {
    label += s_serialLen ? " (live)" : " (type in serial)";
  }
  tft.drawString(label, 8, 46, 2);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  String show = s_text;
  while (show.length() > 3 && tft.textWidth(show, 1) > tft.width() - 16) {
    show.remove(0, 1);
  }
  tft.drawString((show.length() < (unsigned)strlen(s_text) ? "..." : "") + show, 8, 64, 1);

  if (!s_text[0]) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.drawString("send a line over USB serial", tft.width() / 2, 160, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }

  // Version 10 = 57x57 modules -> (57*57+7)/8 bytes; qrcode_getBufferSize
  // is not constexpr, so the max size is spelled out.
  static uint8_t s_qrModules[(57 * 57 + 7) / 8];
  static QRCode s_qr;
  bool ok = false;
  for (uint8_t v = 1; v <= 10 && !ok; v++) {
    ok = (qrcode_initText(&s_qr, s_qrModules, v, ECC_MEDIUM, s_text) == 0);
  }
  if (!ok) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.drawString("text too long for QR", tft.width() / 2, 160, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }
  const int modules = s_qr.size;
  // Quiet zone of 4 modules, scaled to the biggest square that fits.
  const int box = 216;
  const int total = modules + 8;
  const int scale = box / total;
  if (scale < 1) return;
  const int side = total * scale;
  const int x0 = (tft.width() - side) / 2;
  const int y0 = 72 + (226 - 72 - side) / 2 + 4;

  tft.fillRect(x0, y0, side, side, TFT_WHITE);
  for (int y = 0; y < modules; y++) {
    for (int x = 0; x < modules; x++) {
      if (qrcode_getModule(&s_qr, x, y)) {
        tft.fillRect(x0 + (x + 4) * scale, y0 + (y + 4) * scale, scale, scale, TFT_BLACK);
      }
    }
  }
}

void qrSetup() {
  extraChrome("QR CODE");
  s_preset = 0;
  s_serialLen = 0;
  s_serial[0] = 0;
  applyPreset();
  renderQr();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("UP/DOWN = preset", tft.width() / 2, 262, 1);
  tft.setTextDatum(TL_DATUM);
}

void qrLoop() {
  if (extraExitRequested()) return;

  const uint32_t now = millis();
  if (now - s_lastBtnMs > kBtnDebounce) {
    if (isTouchNavButtonPressedEdge(BTN_UP) || isButtonPressed(BTN_UP)) {
      s_lastBtnMs = now;
      s_preset = (s_preset + kPresetCount - 1) % kPresetCount;
      applyPreset();
      renderQr();
    } else if (isTouchNavButtonPressedEdge(BTN_DOWN) || isButtonPressed(BTN_DOWN)) {
      s_lastBtnMs = now;
      s_preset = (s_preset + 1) % kPresetCount;
      applyPreset();
      renderQr();
    }
  }

  serialConsoleEnabled = (s_preset != 3);   // serial-input preset owns USB
  if (s_preset == 3) {
    bool changed = false;
    while (Serial.available()) {
      const char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (s_serialLen > 0) {
          s_serial[s_serialLen] = 0;
          applyPreset();
          renderQr();
          changed = false;
          continue;
        }
        continue;
      }
      if (s_serialLen < (int)sizeof(s_serial) - 1) {
        s_serial[s_serialLen++] = c;
        changed = true;
      }
    }
    if (changed) {
      // Live preview of the buffer being typed.
      s_serial[s_serialLen] = 0;
      applyPreset();
      renderQr();
    }
  }
  delay(30);
}

void exit() { serialConsoleEnabled = true; }

}  // namespace QrTool

// ──────────────────────────────────────────────────── Channel Analyzer ──
// Measures how busy each 2.4 GHz WiFi channel is by dwelling on every
// channel in promiscuous mode and counting airtime bytes. Diagnose crowded
// channels before deploying an AP, or watch interference appear live.
namespace ChannelAnalyzer {

static constexpr int kChannels = 13;      // 1..13
static constexpr uint16_t kDwellMs = 130;  // per channel, per sweep

static volatile uint32_t s_bytesThisCh = 0;
static volatile uint32_t s_framesThisCh = 0;
static uint32_t s_bytes[kChannels];
static uint32_t s_frames[kChannels];
static uint8_t  s_currentCh = 1;
static uint32_t s_chUntil = 0;
static int      s_phase = 0;              // 0..kChannels sweep index
static uint32_t s_lastBtnMs = 0;
static constexpr uint32_t kBtnDebounce = 250;
static bool s_wasStaMode = true;

static void IRAM_ATTR promCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA && type != WIFI_PKT_CTRL) return;
  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  s_bytesThisCh += pkt->rx_ctrl.sig_len;
  s_framesThisCh++;
}

static void drawBars() {
  tft.fillRect(0, 42, tft.width(), 220, TFT_BLACK);

  uint32_t peak = 0;
  int peakCh = 1;
  for (int i = 0; i < kChannels; i++) {
    if (s_bytes[i] > peak) { peak = s_bytes[i]; peakCh = i + 1; }
  }
  const uint32_t ref = peak > 0 ? peak : 1;

  char l[40];
  snprintf(l, sizeof(l), "busiest: ch %d", peakCh);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(l, 8, 46, 2);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("bytes per 130 ms dwell", 8, 66, 1);

  // Vertical bars, 13 channels across 224 px.
  const int bw = 14, gap = 3;
  const int x0 = 8, baseY = 200, maxH = 100;
  for (int i = 0; i < kChannels; i++) {
    const int h = (int)((s_bytes[i] * maxH) / ref);
    const int x = x0 + i * (bw + gap);
    tft.fillRect(x, baseY - h, bw, h,
                 s_bytes[i] == peak && peak > 0 ? UI_ICON : UI_LABLE);
    tft.drawRect(x, baseY - maxH, bw, maxH, UI_LINE);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.drawString(String(i + 1), x + bw / 2, baseY + 4, 1);
    tft.setTextDatum(TL_DATUM);
  }
  tft.drawFastHLine(0, baseY + 16, tft.width(), UI_LINE);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("move your AP to the shortest bar", 8, baseY + 22, 1);
}

void chanSetup() {
  pauseBackgroundRadioTasks();
  extraChrome("CH ANALYZER");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("scanning channels...", tft.width() / 2, 160, 2);
  tft.setTextDatum(TL_DATUM);

  s_wasStaMode = (WiFi.getMode() & WIFI_MODE_STA) != 0;
  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&promCb);
  esp_wifi_set_promiscuous(true);

  memset(s_bytes, 0, sizeof(s_bytes));
  memset(s_frames, 0, sizeof(s_frames));
  s_bytesThisCh = 0;
  s_framesThisCh = 0;
  s_phase = 0;
  s_currentCh = 1;
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  s_chUntil = millis() + kDwellMs;
}

void chanLoop() {
  if (extraExitRequested()) return;

  if ((int32_t)(millis() - s_chUntil) >= 0) {
    // Store the finished dwell, then move to the next channel.
    s_bytes[s_phase] = s_bytesThisCh;
    s_frames[s_phase] = s_framesThisCh;
    s_bytesThisCh = 0;
    s_framesThisCh = 0;
    s_phase++;
    if (s_phase >= kChannels) {
      s_phase = 0;
      drawBars();
    }
    s_currentCh = s_phase + 1;
    esp_wifi_set_channel(s_currentCh, WIFI_SECOND_CHAN_NONE);
    s_chUntil = millis() + kDwellMs;
  }

  delay(10);
}

void exit() {
  esp_wifi_set_promiscuous(false);
  if (!s_wasStaMode) {
    WiFi.mode(WIFI_MODE_STA);  // the rest of the firmware expects STA anyway
  }
}

}  // namespace ChannelAnalyzer
