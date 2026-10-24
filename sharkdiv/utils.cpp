#include <SD.h>
#include <SPI.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "driver/gpio.h"
#include "Touchscreen.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icon.h"
#include "gps.h"
#include "shared.h"
#include "SharkUI.h"
#include "utils.h"
#include "DetectionAlerts.h"

bool has_ip5306 = false;

bool notificationVisible = false;
static bool notificationHasSave = false;
static int notifX, notifY, notifWidth, notifHeight;
static int closeButtonX, closeButtonY, closeButtonSize = 16;
static int okButtonX, okButtonY, okButtonWidth = 84, okButtonHeight = 24;
static int saveButtonX, saveButtonY, saveButtonWidth = 84, saveButtonHeight = 24;

static char s_notifTitleCache[64];
static char s_notifMsgCache[400];
static bool s_notifShowSaveCache = false;

/** Wrapped line count for modal text (font 1, size 1 — must match draw path). */
static int countWrappedNotifLines(const String& msgIn, int innerPixelW) {
  tft.setTextFont(1);
  tft.setTextSize(1);
  String msg = msgIn;
  msg.trim();
  int lines = 0;
  while (msg.length() > 0 && lines < 48) {
    int lineEnd = msg.length();
    while (lineEnd > 0 && tft.textWidth(msg.substring(0, lineEnd)) > innerPixelW) {
      lineEnd--;
    }
    if (lineEnd <= 0) {
      ++lines;
      break;
    }
    if (lineEnd < msg.length()) {
      const int lastSpace = msg.substring(0, lineEnd).lastIndexOf(' ');
      if (lastSpace > 0) {
        lineEnd = lastSpace;
      }
    }
    ++lines;
    msg = msg.substring(lineEnd);
    msg.trim();
  }
  return lines;
}

static size_t decodeObfTo(char* out, size_t outSize, const uint8_t* in, size_t inLen, uint8_t key) {
  if (!out || outSize == 0) return 0;
  size_t n = inLen;
  if (n > outSize - 1) n = outSize - 1;
  for (size_t i = 0; i < n; ++i) out[i] = (char)(in[i] ^ key);
  out[n] = '\0';
  return n;
}

void tftPrintObf(const uint8_t* data, size_t len, uint8_t key) {
  char buf[96];
  decodeObfTo(buf, sizeof(buf), data, len, key);
  tft.print(buf);
}

void tftPrintlnObf(const uint8_t* data, size_t len, uint8_t key) {
  char buf[96];
  decodeObfTo(buf, sizeof(buf), data, len, key);
  tft.println(buf);
}

void serialPrintObf(const uint8_t* data, size_t len, bool newline, uint8_t key) {
  char buf[128];
  decodeObfTo(buf, sizeof(buf), data, len, key);
  if (newline) Serial.println(buf);
  else Serial.print(buf);
}

static inline bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
  return x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh);
}

static void drawNotificationInternal(const char* title, const char* message, bool showSave) {
    snprintf(s_notifTitleCache, sizeof(s_notifTitleCache), "%s", title ? title : "");
    snprintf(s_notifMsgCache, sizeof(s_notifMsgCache), "%s", message ? message : "");
    s_notifShowSaveCache = showSave;
    notificationHasSave = showSave;

    const int scrW = tft.width();
    const int scrH = tft.height();
    notifWidth = (scrW > 232) ? 224 : (scrW - 16);
    if (notifWidth < 160) {
        notifWidth = 160;
    }

    const int messageBoxWidth = notifWidth - 10;
    const int innerTextW = messageBoxWidth - 6;
    const int lineH = 12;
    const int footH = showSave ? 40 : 30;
    const int topBand = 25;

    tft.setTextFont(1);
    tft.setTextSize(1);
    String msgProbe = message ? String(message) : String("");
    msgProbe.trim();
    int lineCount = countWrappedNotifLines(msgProbe, innerTextW);
    if (lineCount < 2) {
        lineCount = 2;
    }
    const int maxLinesByScreen = (scrH - topBand - footH - 14) / lineH;
    int useLines = lineCount;
    if (useLines > maxLinesByScreen) {
        useLines = maxLinesByScreen > 2 ? maxLinesByScreen : 2;
    }

    const int messageBoxHeight = 8 + useLines * lineH;
    notifHeight = topBand + messageBoxHeight + footH;
    if (notifHeight > scrH - 8) {
        notifHeight = scrH - 8;
        const int availBody = notifHeight - topBand - footH;
        useLines = (availBody - 8) / lineH;
        if (useLines < 2) {
            useLines = 2;
        }
    }
    notifX = (scrW - notifWidth) / 2;
    notifY = (scrH - notifHeight) / 2;
    if (notifY < 4) {
        notifY = 4;
    }

    tft.fillRect(notifX, notifY, notifWidth, notifHeight, LIGHT_GRAY);
    tft.drawRect(notifX, notifY, notifWidth, notifHeight, DARK_GRAY);
    tft.fillRect(notifX, notifY, notifWidth, 20, BLUE);

    tft.setTextColor(WHITE, BLUE);
    tft.setTextSize(1);
    tft.setCursor(notifX + 6, notifY + 5);
    tft.print(title);

    closeButtonX = notifX + notifWidth - closeButtonSize - 6;
    closeButtonY = notifY + 3;
    tft.fillRect(closeButtonX, closeButtonY, closeButtonSize, closeButtonSize, RED);
    tft.setTextColor(WHITE, RED);
    tft.setCursor(closeButtonX + 5, closeButtonY + 4);
    tft.print("X");

    int messageBoxX = notifX + 5;
    int messageBoxY = notifY + 25;
    const int messageBoxDrawH = notifHeight - topBand - footH;
    tft.fillRect(messageBoxX, messageBoxY, messageBoxWidth, messageBoxDrawH, WHITE);
    tft.setTextColor(BLACK, WHITE);

    {
      const int maxY = messageBoxY + messageBoxDrawH - lineH;
      String msg = message ? String(message) : String("");
      msg.trim();
      int cursorX = messageBoxX + 3;
      int cursorY = messageBoxY + 4;
      while (msg.length() > 0 && cursorY <= maxY) {
        int lineEnd = msg.length();
        while (lineEnd > 0 && tft.textWidth(msg.substring(0, lineEnd)) > innerTextW) {
          lineEnd--;
        }
        if (lineEnd <= 0) {
          break;
        }
        if (lineEnd < msg.length()) {
          int lastSpace = msg.substring(0, lineEnd).lastIndexOf(' ');
          if (lastSpace > 0) {
            lineEnd = lastSpace;
          }
        }
        tft.setCursor(cursorX, cursorY);
        tft.print(msg.substring(0, lineEnd));
        msg = msg.substring(lineEnd);
        msg.trim();
        cursorY += lineH;
      }

      if (msg.length() > 0) {
        tft.setCursor(cursorX, maxY);
        tft.print("...");
      }
    }

    int btnY = notifY + notifHeight - 25;
    if (showSave) {
      okButtonWidth = 72;
      saveButtonWidth = 72;
      okButtonHeight = saveButtonHeight = 20;
      okButtonX = notifX + 10;
      saveButtonX = notifX + notifWidth - 10 - saveButtonWidth;
      okButtonY = saveButtonY = btnY;

      tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
      tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
      tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
      tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);
      tft.setTextColor(BLACK, GRAY);
      tft.setCursor(okButtonX + 28, okButtonY + 5);
      tft.print("OK");

      tft.fillRect(saveButtonX, saveButtonY, saveButtonWidth, saveButtonHeight, GREEN);
      tft.drawRect(saveButtonX, saveButtonY, saveButtonWidth, saveButtonHeight, DARK_GRAY);
      tft.drawLine(saveButtonX, saveButtonY, saveButtonX + saveButtonWidth, saveButtonY, WHITE);
      tft.drawLine(saveButtonX, saveButtonY, saveButtonX, saveButtonY + saveButtonHeight, WHITE);
      tft.setTextColor(BLACK, GREEN);
      tft.setCursor(saveButtonX + 18, saveButtonY + 5);
      tft.print("SAVE");
    } else {
      okButtonWidth = 72;
      okButtonHeight = 20;
      okButtonX = notifX + (notifWidth - okButtonWidth) / 2;
      okButtonY = btnY;
      tft.fillRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, GRAY);
      tft.drawRect(okButtonX, okButtonY, okButtonWidth, okButtonHeight, DARK_GRAY);
      tft.drawLine(okButtonX, okButtonY, okButtonX + okButtonWidth, okButtonY, WHITE);
      tft.drawLine(okButtonX, okButtonY, okButtonX, okButtonY + okButtonHeight, WHITE);
      tft.setTextColor(BLACK, GRAY);
      tft.setCursor(okButtonX + 28, okButtonY + 5);
      tft.print("OK");
    }

    notificationVisible = true;
}

void showNotificationActions(const char* title, const char* message, bool showSave) {
    drawNotificationInternal(title, message, showSave);
}

void showNotification(const char* title, const char* message) {
    drawNotificationInternal(title, message, false);
}

void hideNotification() {
    applyThemeToPalette(settings().theme);
    tft.fillRect(notifX, notifY, notifWidth, notifHeight, UI_BG);
    notificationVisible = false;
}

bool isNotificationVisible() { return notificationVisible; }

void notificationRedrawIfVisible() {
    if (!notificationVisible) {
        return;
    }
    drawNotificationInternal(s_notifTitleCache, s_notifMsgCache, s_notifShowSaveCache);
}

NotificationAction notificationHandleTouch(int x, int y) {
  if (!notificationVisible) return NotificationAction::None;

  if (inRect(x, y, closeButtonX, closeButtonY, closeButtonSize, closeButtonSize)) {
    hideNotification();
    return NotificationAction::Close;
  }
  if (inRect(x, y, okButtonX, okButtonY, okButtonWidth, okButtonHeight)) {
    hideNotification();
    return NotificationAction::Ok;
  }
  if (notificationHasSave && inRect(x, y, saveButtonX, saveButtonY, saveButtonWidth, saveButtonHeight)) {
    hideNotification();
    return NotificationAction::Save;
  }
  return NotificationAction::None;
}

void printWrappedText(int x, int y, int maxWidth, const char* text) {
    String message = text;
    int cursorX = x, cursorY = y;

    while (message.length() > 0) {
        int lineEnd = message.length();

        while (tft.textWidth(message.substring(0, lineEnd)) > maxWidth) {
            lineEnd--;
        }

        if (lineEnd < message.length()) {
            int lastSpace = message.substring(0, lineEnd).lastIndexOf(' ');
            if (lastSpace > 0) lineEnd = lastSpace;
        }

        tft.setCursor(cursorX, cursorY);
        tft.print(message.substring(0, lineEnd));

        message = message.substring(lineEnd);
        message.trim();

        cursorY += 15;
    }
}

namespace FeatureUI {

#ifndef FEATURE_TEXT
#define FEATURE_TEXT ORANGE
#endif

static inline uint16_t btnFill(ButtonStyle style, bool pressed, bool disabled) {
  if (disabled) return UI_LINE;
  if (pressed)  return UI_FG;
  switch (style) {
    case ButtonStyle::Primary:   return FEATURE_TEXT;
    case ButtonStyle::Secondary: return UI_FG;
    case ButtonStyle::Danger:    return UI_WARN;
  }
  return UI_FG;
}

static inline uint16_t btnBorder(ButtonStyle style, bool disabled) {
  if (disabled) return UI_LINE;
  switch (style) {
    case ButtonStyle::Primary:   return FEATURE_TEXT;
    case ButtonStyle::Secondary: return FEATURE_TEXT;
    case ButtonStyle::Danger:    return UI_WARN;
  }
  return FEATURE_TEXT;
}

static inline uint16_t btnText(ButtonStyle style, bool disabled) {
  if (disabled) return UI_TEXT;

  switch (style) {
    case ButtonStyle::Secondary: return UI_ICON;
    case ButtonStyle::Primary:
    case ButtonStyle::Danger:    return FEATURE_BG;
  }
  return UI_ICON;
}

void drawFooterBg() {

  tft.fillRect(0, tft.height() - FOOTER_H, tft.width(), FOOTER_H, FEATURE_BG);
}

void drawButtonRect(int x, int y, int w, int h,
                    const char* label,
                    ButtonStyle style,
                    bool pressed,
                    bool disabled,
                    uint8_t font) {
  if (!label) label = "";
  uint16_t fill = btnFill(style, pressed, disabled);
  uint16_t edge = btnBorder(style, disabled);
  uint16_t txt  = btnText(style, disabled);

  tft.fillRect(x, y, w, h, fill);
  tft.drawRect(x, y, w, h, edge);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(txt, fill);

  // Feature callers may leave a different font/scale active. Measure exactly
  // what will be drawn and keep long labels inside their touch target.
  tft.setTextSize(1);
  tft.setTextFont(font);
  String fitted(label);
  if (tft.textWidth(fitted, font) > w - 8) {
    font = 1; tft.setTextFont(font);
    while (fitted.length() && tft.textWidth(fitted + "~", font) > w - 8)
      fitted.remove(fitted.length() - 1);
    fitted += "~";
  }
  tft.drawString(fitted, x + w/2, y + h/2, font);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

void layoutFooter3(Button (&btns)[3],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   const char* l2, ButtonStyle s2,
                   bool d0, bool d1, bool d2) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - 2*GAP_X) / 3;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  int x2 = x1 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
  btns[2] = {(int16_t)x2,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l2,s2,d2};
}

void layoutFooter2(Button (&btns)[2],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   bool d0, bool d1) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - GAP_X) / 2;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
}

void layoutFooter4(Button (&btns)[4],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   const char* l2, ButtonStyle s2,
                   const char* l3, ButtonStyle s3,
                   bool d0, bool d1, bool d2, bool d3) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - 3*GAP_X) / 4;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  int x2 = x1 + w + GAP_X;
  int x3 = x2 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
  btns[2] = {(int16_t)x2,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l2,s2,d2};
  btns[3] = {(int16_t)x3,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l3,s3,d3};
}

void layoutFooter1(Button& btn, const char* label, ButtonStyle style, bool disabled) {
  int x = PAD_X;
  int w = tft.width() - 2*PAD_X;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  btn = {(int16_t)x,(int16_t)y,(int16_t)w,(int16_t)BTN_H,label,style,disabled};
}

void layoutFooter5(Button (&btns)[5],
                   const char* l0, ButtonStyle s0,
                   const char* l1, ButtonStyle s1,
                   const char* l2, ButtonStyle s2,
                   const char* l3, ButtonStyle s3,
                   const char* l4, ButtonStyle s4,
                   bool d0, bool d1, bool d2, bool d3, bool d4) {
  int availW = tft.width() - 2*PAD_X;
  int w = (availW - 4*GAP_X) / 5;
  int y = tft.height() - FOOTER_H + (FOOTER_H - BTN_H)/2;
  int x0 = PAD_X;
  int x1 = x0 + w + GAP_X;
  int x2 = x1 + w + GAP_X;
  int x3 = x2 + w + GAP_X;
  int x4 = x3 + w + GAP_X;
  btns[0] = {(int16_t)x0,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l0,s0,d0};
  btns[1] = {(int16_t)x1,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l1,s1,d1};
  btns[2] = {(int16_t)x2,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l2,s2,d2};
  btns[3] = {(int16_t)x3,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l3,s3,d3};
  btns[4] = {(int16_t)x4,(int16_t)y,(int16_t)w,(int16_t)BTN_H,l4,s4,d4};
}

int hit(const Button* btns, int n, int x, int y) {
  for (int i = 0; i < n; ++i) {
    const auto& b = btns[i];
    if (x >= b.x && x <= (b.x + b.w) && y >= b.y && y <= (b.y + b.h)) return i;
  }
  return -1;
}

}

unsigned long lastStatusBarUpdate = 0;
const int STATUS_BAR_UPDATE_INTERVAL = 2000;
float lastBatteryVoltage = 0.0;
bool sdCardPresent = false;
bool lastSdCardState = false;

static TaskHandle_t statusBarTaskHandle = nullptr;
static volatile bool statusBarDirty = true;
static constexpr uint32_t kStatusBarWardBlinkHalfMs = 900;

void requestStatusBarRedraw() {
  statusBarDirty = true;
}

const float R1 = 100000.0;
const float R2 = 100000.0;

float readBatteryVoltage()
{
  static bool adcInitialized = false;

  if (!adcInitialized)
  {
    analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
    adcInitialized = true;
  }

  const int sampleCount = 16;
  uint32_t sum = 0;

  for (int i = 0; i < sampleCount; i++)
  {
    sum += analogReadMilliVolts(BATTERY_ADC_PIN);
    delayMicroseconds(500);
  }

  float avgMv = sum / (float)sampleCount;

  return (avgMv / 1000.0f) * 2.0f;
}

float readInternalTemperature() {
#if DIV_VM_BUILD
  // QEMU: the temperature sensor's RTC registers are unbacked and polling
  // them never completes — return a room-temperature constant instead.
  return 25.0f;
#else
  float temperature = temperatureRead();
  return temperature;
#endif
}

void updateSdCardStatus() {

#ifdef SD_CD
  bool cardDetected = !digitalRead(SD_CD);
#else
  bool cardDetected = true;
#endif

  if (cardDetected != lastSdCardState) {
    sdCardPresent = cardDetected;
    lastSdCardState = cardDetected;

    Serial.println(sdCardPresent ? "SD Card detected" : "SD Card removed");
    requestStatusBarRedraw();
  }
}

static bool statusBarBatteryMeaningfulChange(int newPct, int oldPct, bool force) {
  if (force || oldPct == -1) {
    return true;
  }
  if (newPct <= 25 || oldPct <= 25) {
    return newPct != oldPct;
  }
  const int d = newPct - oldPct;
  return (d >= 3 || d <= -3);
}

static int statusBarTempBand(float t) {
  if (fabs(static_cast<double>(t) - 53.33) < 0.51) {
    return 1;
  }
  if (t > 55.f) {
    return 2;
  }
  return 0;
}

// Title on the left of the status bar. Each menu screen sets its own
// ("SHARK"+"DIV" on home, the category on submenus); feature screens keep the
// last one, so a WiFi tool still reads "WiFi" up top.
static const char* s_barTitle  = "SHARK";
static const char* s_barAccent = "DIV";
static int s_wardX = -1;   // x of the blinking wardriver icon, -1 when not drawn

void setStatusTitle(const char* title, const char* accent) {
  s_barTitle  = title ? title : "";
  s_barAccent = accent;
}

void drawStatusBar(float batteryVoltage, bool forceUpdate, bool bottomSeparator) {
  static int lastBatteryPercentage = -1;
  static int lastWifiHalf          = -100000;
  static int lastBleHalf           = -100000;
  static int lastTempBand          = -100;
  static int lastSdSnap            = -1;
  static bool lastWardGpsIcon      = false;
  static uint32_t lastWardBlinkPhase = 0;

  /*
  int batteryPercentage = ::map(batteryVoltage * 100, 300, 420, 0, 100);
  */
  /*调用IP5306*/
  // Reg 0x78 high nibble is the fuel gauge. Not every IP5306 firmware reports
  // only the four LilyGO codes (00/80/C0/E0) — intermediate steps occur, and a
  // failed read used to snap the icon to 0%. Map by range and keep the last
  // good value on I2C failure instead.
  static int lastIpPercent = -1;
  int batteryPercentage = lastIpPercent;
  if (has_ip5306) {
    Wire.beginTransmission(IP5306_ADDR);
    Wire.write(0x78);
    if (Wire.endTransmission(false) == 0 && Wire.requestFrom(IP5306_ADDR, 1) == 1) {
      switch (Wire.read() & 0xF0) {
        case 0x00: batteryPercentage = 100; break;
        case 0x20: batteryPercentage = 90;  break;
        case 0x40: batteryPercentage = 75;  break;
        case 0x60: batteryPercentage = 60;  break;
        case 0x80: batteryPercentage = 50;  break;
        case 0xA0: batteryPercentage = 40;  break;
        case 0xC0: batteryPercentage = 25;  break;
        case 0xE0: batteryPercentage = 15;  break;
        default:   batteryPercentage = 5;   break;
      }
      if (batteryPercentage >= 0) lastIpPercent = batteryPercentage;
    }
  }
  if (batteryPercentage < 0) batteryPercentage = 0;

  batteryPercentage = constrain(batteryPercentage, 0, 100);

  int wifiDevices = 0;
  int bleDevices  = 0;

  wifiDevices = WifiScan::getLastCount();
  bleDevices  = BleScan::getLastCount();

  const int wifiHalf = wifiDevices / 2;
  const int bleHalf  = bleDevices / 2;

  const bool wardGpsIcon = GpsWardriver::statusBarGpsIconActive();
  const uint32_t wardBlinkPhase =
      wardGpsIcon ? (millis() / kStatusBarWardBlinkHalfMs) : 0u;
  const bool wardSatVisible =
      wardGpsIcon && ((wardBlinkPhase & 1u) == 0u);

  const float internalTemp = readInternalTemperature();
  const int tempBand         = statusBarTempBand(internalTemp);
  const int sdSnap           = sdCardPresent ? 1 : 0;

  const bool battCh =
      statusBarBatteryMeaningfulChange(batteryPercentage, lastBatteryPercentage, forceUpdate);
  const bool wardBlinkOnly =
      !forceUpdate && wardGpsIcon && lastWardGpsIcon &&
      (wardBlinkPhase != lastWardBlinkPhase) && !battCh && wifiHalf == lastWifiHalf &&
      bleHalf == lastBleHalf && tempBand == lastTempBand && sdSnap == lastSdSnap;

  if (wardBlinkOnly) {
    if (s_wardX >= 0) {
      if (wardSatVisible) {
        tft.drawBitmap(s_wardX, 2, bitmap_icon_satellite, 16, 16, UI_WARN);
      } else {
        tft.fillRect(s_wardX, 2, 16, 16, UI_LABLE);
      }
    }
    lastWardBlinkPhase = wardBlinkPhase;
    return;
  }

  if (battCh || wifiHalf != lastWifiHalf || bleHalf != lastBleHalf || tempBand != lastTempBand ||
      sdSnap != lastSdSnap || wardGpsIcon != lastWardGpsIcon ||
      (wardGpsIcon && wardBlinkPhase != lastWardBlinkPhase) || forceUpdate) {
    const int barHeight = 20;
    tft.fillRect(0, 0, tft.width(), barHeight, UI_LABLE);

    // Left: screen title in bold sans with the accent word in brand green.
    // A title too long for the gap before the icons drops to font 2.
    tft.setFreeFont(&FreeSansBold9pt7b);
    int titleW  = tft.textWidth(s_barTitle);
    int accentW = s_barAccent ? tft.textWidth(s_barAccent) : 0;
    if (titleW + accentW > 96) {
      tft.setTextFont(2);
      titleW  = tft.textWidth(s_barTitle);
      accentW = s_barAccent ? tft.textWidth(s_barAccent) : 0;
    }
    tft.setTextDatum(L_BASELINE);
    tft.setTextColor(UI_TEXT, UI_LABLE);
    tft.drawString(s_barTitle, 8, 15);
    if (s_barAccent) {
      tft.setTextColor(UI_ICON, UI_LABLE);
      tft.drawString(s_barAccent, 8 + titleW, 15);
    }
    const int titleEnd = 8 + titleW + accentW;
    tft.setTextFont(1);
    tft.setTextSize(1);

    // Right: battery outline, level and percent.
    const int bx = 189, by = 5;
    tft.drawRoundRect(bx, by, 20, 10, 2, UI_TEXT);
    tft.fillRect(bx + 20, by + 3, 2, 4, UI_TEXT);
    tft.fillRect(bx + 2, by + 2, ::map(batteryPercentage, 0, 100, 0, 16), 6,
                 batteryPercentage > 20 ? UI_ICON : TFT_RED);
    tft.setTextColor(UI_TEXT, UI_LABLE);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(String(batteryPercentage) + "%", 238, 10);
    tft.setTextDatum(TL_DATUM);

    // Radio activity: BLE glyph and four Wi-Fi bars, green when something is seen.
    tft.drawBitmap(170, 2, bitmap_icon_ble, 16, 16, bleDevices > 0 ? UI_ICON : UI_DIM_TEXT);
    const int wifiStrength =
        wifiDevices > 0 ? constrain(::map(wifiDevices, 0, 15, 0, 100), 0, 100) : 0;
    for (int i = 0; i < 4; i++) {
      const int h = (i + 1) * 3, wx = 146 + i * 6;
      if (wifiStrength > i * 25) tft.fillRect(wx, 15 - h, 4, h, UI_ICON);
      else                       tft.drawRect(wx, 15 - h, 4, h, UI_DIM_TEXT);
    }

    // Alerts only when they need attention — the blinking wardriver GPS, an odd
    // or hot sensor temperature, a missing SD card — flowed after the title.
    int ax = titleEnd + 6;
    s_wardX = -1;
    if (wardGpsIcon && ax + 16 <= 142) {
      s_wardX = ax;
      if (wardSatVisible) tft.drawBitmap(ax, 2, bitmap_icon_satellite, 16, 16, UI_WARN);
      ax += 18;
    }
    if (tempBand != 0 && ax + 16 <= 142) {
      tft.drawBitmap(ax, 2, bitmap_icon_temp, 16, 16, tempBand == 2 ? TFT_RED : UI_WARN);
      ax += 18;
    }
    if (!sdCardPresent && ax + 16 <= 142) {
      tft.drawBitmap(ax, 2, bitmap_icon_nullsdcard, 16, 16, TFT_RED);
    }

    // Hairline under the bar, green under the title — drawn on every redraw
    // since the background refresh passes no separator flag.
    tft.drawFastHLine(0, barHeight - 1, tft.width(), UI_LINE);
    tft.drawFastHLine(0, barHeight - 1, titleEnd + 4, UI_ICON);
    (void)bottomSeparator;

    lastBatteryPercentage = batteryPercentage;
    lastWifiHalf          = wifiHalf;
    lastBleHalf           = bleHalf;
    lastTempBand          = tempBand;
    lastSdSnap            = sdSnap;
    lastWardGpsIcon       = wardGpsIcon;
    lastWardBlinkPhase    = wardGpsIcon ? wardBlinkPhase : 0u;
  }
}

static void statusBarTask(void* ) {
  static int prevBattPct    = -1;
  static int prevWifiHalf   = -100000;
  static int prevBleHalf    = -100000;
  static int prevTempBand   = -100;
  static int prevSdSnap     = -1;
  static bool prevWardIcon  = false;
  static uint32_t prevWardPhase = 0;

  for (;;) {
    updateSdCardStatus();
    const float v = readBatteryVoltage();
    currentBatteryVoltage = v;

    const int pct = constrain(::map((int)(v * 100.f), 300, 420, 0, 100), 0, 100);
    const int wifi  = WifiScan::getLastCount();
    const int ble   = BleScan::getLastCount();
    const int wifiH = wifi / 2;
    const int bleH  = ble / 2;
    const int tBand = statusBarTempBand(readInternalTemperature());
    const int sdSn  = sdCardPresent ? 1 : 0;
    const bool ward = GpsWardriver::statusBarGpsIconActive();
    const uint32_t wPh = ward ? (millis() / kStatusBarWardBlinkHalfMs) : 0u;

    bool need = false;
    if (statusBarBatteryMeaningfulChange(pct, prevBattPct, false)) {
      need = true;
    }
    if (wifiH != prevWifiHalf || bleH != prevBleHalf) {
      need = true;
    }
    if (tBand != prevTempBand || sdSn != prevSdSnap) {
      need = true;
    }
    if (ward != prevWardIcon) {
      need = true;
    }
    if (ward && wPh != prevWardPhase) {
      need = true;
    }

    if (need) {
      statusBarDirty = true;
      prevBattPct     = pct;
      prevWifiHalf    = wifiH;
      prevBleHalf     = bleH;
      prevTempBand    = tBand;
      prevSdSnap      = sdSn;
      prevWardIcon    = ward;
      prevWardPhase   = ward ? wPh : 0u;
    }

    vTaskDelay(pdMS_TO_TICKS(400));
  }
}

void startStatusBarTask() {
  if (statusBarTaskHandle != nullptr) return;
  xTaskCreatePinnedToCore(
    statusBarTask,
    "statusBar",
    2048,
    nullptr,
    1,
    &statusBarTaskHandle,
    0
  );
}

void pauseBackgroundRadioTasks() {
  GpsWardriver::stopBackgroundIfRunning();
  // Callers mark their feature active before entering. Wait for any background
  // operation that already passed its idle check before reconfiguring radios.
  WifiScan::waitForBackgroundScanner();
  BleScan::waitForBackgroundScanner();
}

void updateStatusBar() {

  if (statusBarTaskHandle != nullptr) {
    if (statusBarDirty) {
      drawStatusBar(currentBatteryVoltage, false);
      statusBarDirty = false;
    }
    return;
  }

  const unsigned long currentMillis = millis();
  const bool wardOn = GpsWardriver::statusBarGpsIconActive();
  static uint32_t s_noTaskLastWardPhase = UINT32_MAX;
  const uint32_t wardPh =
      wardOn ? (currentMillis / kStatusBarWardBlinkHalfMs) : 0u;
  const bool wardBlinkTick =
      wardOn && (s_noTaskLastWardPhase == UINT32_MAX || wardPh != s_noTaskLastWardPhase);
  if (wardOn) {
    s_noTaskLastWardPhase = wardPh;
  } else {
    s_noTaskLastWardPhase = UINT32_MAX;
  }

  if (currentMillis - lastStatusBarUpdate > STATUS_BAR_UPDATE_INTERVAL || wardBlinkTick) {
    float batteryVoltage = readBatteryVoltage();
    updateSdCardStatus();
    currentBatteryVoltage = batteryVoltage;
    drawStatusBar(batteryVoltage, false);
    lastBatteryVoltage = batteryVoltage;
    lastStatusBarUpdate = currentMillis;
  }
}

#if HAS_PCF8574_BUTTONS
static uint8_t s_pcf8574Addr = 0;

uint8_t getPcf8574Address() {
  return s_pcf8574Addr;
}

bool initPcf8574Buttons() {
  // Prevent I2C bus hangs (no ACK / missing pull-ups) from tripping the task WDT
  // and rebooting right after the intro on classic ESP32.
  Wire.begin();
  Wire.setTimeOut(50);

  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);

#if PCF8574_AUTO_DETECT
  for (uint8_t addr = PCF8574_ADDR_MIN; addr <= PCF8574_ADDR_MAX; addr++) {
    yield();
    if (pcf.begin(addr)) {
      s_pcf8574Addr = addr;
      Serial.printf("[PCF8574] auto-detected at 0x%02X\n", addr);
      break;
    }
  }
  if (s_pcf8574Addr == 0) {
    Serial.println("[PCF8574] not found (scanned 0x20-0x27)");
    return false;
  }
#else
  if (!pcf.begin(PCF8574_I2C_ADDR)) {
    Serial.printf("[PCF8574] not found at fixed address 0x%02X\n", PCF8574_I2C_ADDR);
    return false;
  }
  s_pcf8574Addr = PCF8574_I2C_ADDR;
  Serial.printf("[PCF8574] using fixed address 0x%02X\n", s_pcf8574Addr);
#endif

  return true;
}
#else
uint8_t getPcf8574Address() {
  return 0;
}

bool initPcf8574Buttons() {
  return false;
}
#endif

#if TOUCH_SHARES_TFT_SPI
#if defined(CONFIG_IDF_TARGET_ESP32S3)
static SPIClass s_sdSpi(FSPI);
#elif defined(CONFIG_IDF_TARGET_ESP32)
static SPIClass s_sdSpi(VSPI);
#else
static SPIClass s_sdSpi(FSPI);
#endif
#endif

/** Tracks whether SD.begin() currently owns a live mount on the shared SPI bus. */
static bool s_sdFsMounted = false;

/** GPIO CS that last mounted SD (-1 = unknown). Tried first to avoid long SD.begin on wrong CS. */
static int8_t s_sdLastGoodCs = -1;

/** After a failed mount on classic ESP32, do not keep retrying (SD.begin can WDT). */
static bool s_sdMountGaveUp = false;

void sdRetryMount() {
  s_sdMountGaveUp = false;
}

/** Deselect every other SPI slave that shares the SD bus so none holds MISO. */
static void sdRaiseCsPin(int pin) {
  if (pin < 0) {
    return;
  }
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
}

static void sdReleaseOtherChipSelects() {
#if defined(CC1101_CS)
  sdRaiseCsPin(CC1101_CS);
#endif
#if defined(PN532_SS)
  sdRaiseCsPin(PN532_SS);
#endif
#if defined(CSN_PIN_1)
  sdRaiseCsPin(CSN_PIN_1);
#endif
#if defined(CSN_PIN_2)
  sdRaiseCsPin(CSN_PIN_2);
#endif
#if defined(CSN_PIN_3)
  sdRaiseCsPin(CSN_PIN_3);
#endif
  // Scanner bit-bangs CE/CSN on these pins; keep CE low / CSN high after leaving.
#if defined(CE_PIN_3)
  pinMode(CE_PIN_3, OUTPUT);
  digitalWrite(CE_PIN_3, LOW);
#endif
}

void sdSpiInit() {
#if defined(SD_SCLK) && defined(SD_MISO) && defined(SD_MOSI) && defined(SD_CS)
#if TOUCH_SHARES_TFT_SPI
  s_sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
#else
  // On ESP32-S3 (v2), RFID bitbang remaps these pins — reset before reclaim.
  // On classic ESP32 (v1), SD often shares SPI with TFT_eSPI; gpio_reset_pin
  // after tft.init() can WDT/reboot during boot SD mount.
#if BOARD_HAS_ESP32S3
  gpio_reset_pin((gpio_num_t)SD_SCLK);
  gpio_reset_pin((gpio_num_t)SD_MISO);
  gpio_reset_pin((gpio_num_t)SD_MOSI);
  gpio_reset_pin((gpio_num_t)SD_CS);
#endif
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setFrequency(4000000);
#endif
#endif
}

bool sdMountChipSelect(uint8_t cs) {
#if TOUCH_SHARES_TFT_SPI
  return SD.begin(cs, s_sdSpi, 4000000);
#else
  return SD.begin(cs, SPI, 4000000);
#endif
}

void initSDCard() {

#ifdef SD_CD
  pinMode(SD_CD, INPUT_PULLUP);
#endif

#if !BOARD_HAS_ESP32S3
  // Classic ESP32: raise CS only. Full SD.begin is deferred — boot mount was
  // rebooting right after the intro ("3 sd").
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
  sdReleaseOtherChipSelects();
  // Block auto-mount from settingsLoad / status bar until an SD feature asks.
  s_sdMountGaveUp = true;
  updateSdCardStatus();
  return;
#else
  restoreSdAfterSharedSpi();
  updateSdCardStatus();
#endif
}

static bool sdPinIsOkForSdMount(uint8_t pin) {
#if defined(SD_CS)
  if (pin == SD_CS) {
    return true;
  }
#endif
#if defined(SD_CS_PIN)
#if defined(CC1101_CS)
  if (SD_CS_PIN != CC1101_CS && pin == SD_CS_PIN) {
    return true;
  }
#else
  if (pin == SD_CS_PIN) {
    return true;
  }
#endif
#endif
  return false;
}

static void sdAddPinUnique(uint8_t* pins, int* nTry, uint8_t p) {
  if (!sdPinIsOkForSdMount(p)) {
    return;
  }
  for (int j = 0; j < *nTry; j++) {
    if (pins[j] == p) {
      return;
    }
  }
  if (*nTry < 3) {
    pins[(*nTry)++] = p;
  }
}

/** SPI must already be SD wiring. Returns true if SD.begin succeeded. */
static bool sdTryBeginOrder() {
  uint8_t tryPins[3];
  int nTry = 0;
  if (s_sdLastGoodCs >= 0) {
    sdAddPinUnique(tryPins, &nTry, (uint8_t)s_sdLastGoodCs);
  }
#if defined(SD_CS)
  sdAddPinUnique(tryPins, &nTry, SD_CS);
#endif
#if defined(SD_CS_PIN)
#if defined(CC1101_CS)
  if (SD_CS_PIN != CC1101_CS) {
    sdAddPinUnique(tryPins, &nTry, SD_CS_PIN);
  }
#else
  sdAddPinUnique(tryPins, &nTry, SD_CS_PIN);
#endif
#endif
  for (int i = 0; i < nTry; i++) {
    if (sdMountChipSelect(tryPins[i])) {
      s_sdLastGoodCs = (int8_t)tryPins[i];
      return true;
    }
  }
  s_sdLastGoodCs = -1;
  return false;
}

/** Soft remount: keep current SPI pinmux. Safe while CC1101/nRF24 still need the bus
 *  (same SCK/MISO/MOSI as SD on DIV V2). Only raises other chip-selects and remounts FatFS. */
static bool sdRemountSoft() {
  sdReleaseOtherChipSelects();

#if defined(SD_SCLK) && defined(SD_MISO) && defined(SD_MOSI) && defined(SD_CS)
#if TOUCH_SHARES_TFT_SPI
  s_sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
#else
  // Do not SPI.end()/gpio_reset here — that tears down CC1101 after SubGHz Init.
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
#endif
#endif

  SD.end();
  s_sdFsMounted = false;
  delay(2);
  if (sdTryBeginOrder()) {
    s_sdFsMounted = true;
#if !TOUCH_SHARES_TFT_SPI
    // SD.begin() often leaves the bus at 16–40 MHz; CC1101 cannot use that.
    SPI.setFrequency(4000000);
#endif
    return true;
  }
  return false;
}

bool isSDCardAvailable() {
#ifdef SD_CD
  updateSdCardStatus();
  if (!sdCardPresent) {
    return false;
  }
#endif

  // FatFS "exists(/)" can stay true after another feature stole the SPI bus.
  // cardType() actually talks to the card and catches a dead mount.
  if (s_sdFsMounted) {
    if (SD.cardType() != CARD_NONE) {
      return true;
    }
    s_sdFsMounted = false;
    SD.end();
  }

#if !BOARD_HAS_ESP32S3
  if (s_sdMountGaveUp) {
    return false;
  }
#endif

  // Prefer a soft remount so SubGHz (CC1101 on the same SPI pins) stays alive.
  if (sdRemountSoft()) {
    return true;
  }

  // Soft failed — pins may still be in RFID bitbang / Scanner remapped state.
  // Only do the destructive reclaim when no radio feature owns the bus.
  if (!feature_active) {
    restoreSdAfterSharedSpi();
#if !BOARD_HAS_ESP32S3
    if (!s_sdFsMounted) {
      s_sdMountGaveUp = true;
      Serial.println("[sd] mount failed — will not retry until reboot");
    }
#endif
    return s_sdFsMounted;
  }
  return false;
}

void holdSdInactiveOnSharedSpi() {
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if !TOUCH_SHARES_TFT_SPI
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setFrequency(4000000);
#endif
}

void reclaimSharedSpiBus() {
  // Reset pinmux after RFID bitbang / Scanner remaps, but do NOT SD.begin().
  // Mounting SD here is what broke SubGHz: SD.begin raises SPI clock and parks
  // the card on the same MISO line CC1101 needs.
  sdReleaseOtherChipSelects();
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif
#if defined(PN532_SS)
  pinMode(PN532_SS, OUTPUT);
  digitalWrite(PN532_SS, HIGH);
#endif

  SD.end();
  s_sdFsMounted = false;

#if !TOUCH_SHARES_TFT_SPI
  SPI.end();
#if BOARD_HAS_ESP32S3
#if defined(SD_SCLK) && defined(SD_MISO) && defined(SD_MOSI)
  gpio_reset_pin((gpio_num_t)SD_SCLK);
  gpio_reset_pin((gpio_num_t)SD_MISO);
  gpio_reset_pin((gpio_num_t)SD_MOSI);
#endif
  // PN532 software-SPI may have bitbanged these (on DIV V2 MOSI/MISO are
  // swapped vs SD/CC1101). Reset them even when they overlap SD pins.
#if defined(PN532_SCK)
  gpio_reset_pin((gpio_num_t)PN532_SCK);
#endif
#if defined(PN532_MISO)
  gpio_reset_pin((gpio_num_t)PN532_MISO);
#endif
#if defined(PN532_MOSI)
  gpio_reset_pin((gpio_num_t)PN532_MOSI);
#endif
#if defined(PN532_SS)
  gpio_reset_pin((gpio_num_t)PN532_SS);
  pinMode(PN532_SS, OUTPUT);
  digitalWrite(PN532_SS, HIGH);
#endif
#if defined(SD_CS)
  gpio_reset_pin((gpio_num_t)SD_CS);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  gpio_reset_pin((gpio_num_t)CC1101_CS);
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif
#endif // BOARD_HAS_ESP32S3
#if defined(SD_SCLK) && defined(SD_MISO) && defined(SD_MOSI) && defined(SD_CS)
#if defined(CC1101_SCK) && defined(CC1101_MISO) && defined(CC1101_MOSI) && defined(CC1101_CS)
  // Prefer CC1101 CS as SPI SS — same data pins as SD on DIV V2, but matches
  // what ELECHOUSE SpiStart() will bind on the next Init().
  SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
#else
  SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
#endif
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setFrequency(4000000);
#endif
#else
#if defined(SD_SCLK) && defined(SD_MISO) && defined(SD_MOSI) && defined(SD_CS)
  s_sdSpi.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
#endif
#endif
  delay(2);
}

void restoreSdAfterSharedSpi() {
  // Full reclaim + remount for menu/SD features after leaving SPI radios.
  reclaimSharedSpiBus();
  delay(5);
  if (sdTryBeginOrder()) {
    s_sdFsMounted = true;
#if !TOUCH_SHARES_TFT_SPI
    SPI.setFrequency(4000000);
#endif
  }
  requestStatusBarRedraw();
}

void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center) {
  int16_t bitmapWidth = 100;
  int16_t bitmapHeight = 120;
  int16_t logoX = x;
  int16_t logoY = y;

  if (center) {
    int16_t screenWidth = tft.width();
    int16_t screenHeight = tft.height();
    logoX = (screenWidth - bitmapWidth) / 2;
    logoY = (screenHeight - bitmapHeight) / 2;
  }

  const unsigned char* bitmaps[] = {
    bitmap_icon_skull_loading_1,
    bitmap_icon_skull_loading_2,
    bitmap_icon_skull_loading_3,
    bitmap_icon_skull_loading_4,
    bitmap_icon_skull_loading_5,
    bitmap_icon_skull_loading_6,
    bitmap_icon_skull_loading_7,
    bitmap_icon_skull_loading_8,
    bitmap_icon_skull_loading_9,
    bitmap_icon_skull_loading_10
  };
  const int numFrames = 10;

  for (int r = 0; r < repeats; r++) {
    for (int i = 0; i < numFrames; i++) {
      tft.fillRect(logoX, logoY, bitmapWidth, bitmapHeight, TFT_BLACK);
      tft.drawBitmap(logoX, logoY, bitmaps[i], bitmapWidth, bitmapHeight, color);
      delay(frameDelay);
    }
  }
}

void displayLogo(uint16_t color, int displayTime) {
  UNUSED(color);

  // Brief, static SHARKDIV identity screen.
  playSharkBoot();

  // CiferTech credits stay on the serial console (upstream attribution).
  Serial.println("==================================");
  serialPrintObf(OBF_PN, sizeof(OBF_PN), true);
  Serial.print("Developed by: "); serialPrintObf(OBF_DN, sizeof(OBF_DN), true);
  // Version is intentionally NOT obfuscated.
  Serial.print("Version:      "); Serial.println(ESP32DIV_VERSION);
  Serial.print("Contact:      "); serialPrintObf(OBF_EM, sizeof(OBF_EM), true);
  Serial.print("GitHub:       "); serialPrintObf(OBF_GH, sizeof(OBF_GH), true);
  Serial.print("Website:      "); serialPrintObf(OBF_WB, sizeof(OBF_WB), true);
  Serial.println("==================================");

  UNUSED(displayTime);
}

namespace Terminal {

#define TEXT_HEIGHT 16
#define BOT_FIXED_AREA 0
#define TOP_FIXED_AREA 86
#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320
#define SCREEN_WIDTH 240
#define SCREENHEIGHT 320

static bool uiDrawn = false;

uint16_t yStart = TOP_FIXED_AREA;
uint16_t yArea = DISPLAY_HEIGHT - TOP_FIXED_AREA - BOT_FIXED_AREA;
uint16_t yDraw = DISPLAY_HEIGHT - BOT_FIXED_AREA - TEXT_HEIGHT;

uint16_t xPos = 0;

byte data = 0;

boolean change_colour = 1;
boolean selected = 1;
boolean terminalActive = true;

int blank[19];

long baudRates[] = {9600, 19200, 38400, 57600, 115200};
byte baudIndex = 0;

static int terminalContentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : DISPLAY_HEIGHT;
}

static int terminalBotFixedArea() {
  const int bottom = terminalContentBottom();
  return (bottom < DISPLAY_HEIGHT) ? (DISPLAY_HEIGHT - bottom) : BOT_FIXED_AREA;
}

static void terminalUpdateNavLabels() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  setTouchNavLabels("Exit", nullptr, terminalActive ? "Pause" : "Active", nullptr, "Baud");
  redrawTouchButtonBar();
}

static void terminalCycleBaud() {
  if (terminalActive) {
    terminalActive = false;
    terminalUpdateNavLabels();
    return;
  }
  baudIndex = (baudIndex + 1) % 5;
  Serial.end();
  delay(100);
  Serial.begin(baudRates[baudIndex]);
  tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_WHITE);
  String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
  tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
  delay(10);
}

static void terminalSetActive(bool active) {
  terminalActive = active;
  delay(10);
  tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_WHITE);
  if (active) {
    tft.drawCentreString(" Serial Terminal Active ", DISPLAY_WIDTH / 2, 37, 2);
  } else {
    String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
    tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
  }
  terminalUpdateNavLabels();
}

static void terminalHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    feature_exit_requested = true;
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    terminalCycleBaud();
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    terminalSetActive(!terminalActive);
  }
}

void runUI() {

    #define STATUS_BAR_Y_OFFSET 20
    #define STATUS_BAR_HEIGHT 16
    #define ICON_SIZE 16
    #define ICON_NUM 3

    static int iconX[ICON_NUM] = {210, 170, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_power,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.drawLine(0, 19, 240, 19, TFT_WHITE);
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            }
        }
        tft.drawLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, SCREEN_WIDTH, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, ORANGE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, TFT_WHITE);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                  if (terminalActive) {
                    terminalActive = false;
                  } else if (!terminalActive) {
                    baudIndex = (baudIndex + 1) % 5;
                    Serial.end();
                    delay(100);
                    Serial.begin(baudRates[baudIndex]);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(TFT_WHITE, TFT_WHITE);
                    String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
                    tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);
                    delay(10);
                  }
                    break;
                case 1:
                    delay(10);
                    tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
                    tft.setTextColor(TFT_WHITE, TFT_WHITE);
                    tft.drawCentreString(" Serial Terminal Active ", DISPLAY_WIDTH / 2, 37, 2);
                    terminalActive = true;
                    break;

                case 2:
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        int x, y;
        if (!featureHasTouchNavBar() && feature_active && readTouchXY(x, y)) {
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {
                            tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                            animationState = 1;
                            activeIcon = i;
                            lastAnimationTime = millis();
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

void scrollAddress(uint16_t vsp) {
  tft.writecommand(ILI9341_VSCRSADD);
  tft.writedata(vsp >> 8);
  tft.writedata(vsp);
}

int scroll_line() {
  int yTemp = yStart;
  tft.fillRect(0, yStart, blank[(yStart - TOP_FIXED_AREA) / TEXT_HEIGHT], TEXT_HEIGHT, TFT_BLACK);

  yStart += TEXT_HEIGHT;
  const int scrollLimit = DISPLAY_HEIGHT - terminalBotFixedArea();
  if (yStart >= scrollLimit) {
    yStart = TOP_FIXED_AREA + (yStart - scrollLimit);
  }
  scrollAddress(yStart);
  delay(1);
  return yTemp;
}

void setupScrollArea(uint16_t tfa, uint16_t bfa) {
  tft.writecommand(ILI9341_VSCRDEF);
  tft.writedata(tfa >> 8);
  tft.writedata(tfa);
  tft.writedata((DISPLAY_HEIGHT - tfa - bfa) >> 8);
  tft.writedata(DISPLAY_HEIGHT - tfa - bfa);
  tft.writedata(bfa >> 8);
  tft.writedata(bfa);
}

void terminalSetup() {

  setupTouchscreen();
  tft.fillScreen(TFT_BLACK);

  tft.fillRect(0, 37, DISPLAY_WIDTH, 16, ORANGE);
  tft.setTextColor(TFT_WHITE, TFT_WHITE);
  String baudMsg = " Serial Terminal - " + String(baudRates[baudIndex]) + " baud ";
  tft.drawCentreString(baudMsg, DISPLAY_WIDTH / 2, 37, 2);

  float currentBatteryVoltage = readBatteryVoltage();

  drawStatusBar(currentBatteryVoltage, true);

  uiDrawn = false;

  Serial.begin(baudRates[baudIndex]);

  const int bfa = terminalBotFixedArea();
  yArea = DISPLAY_HEIGHT - TOP_FIXED_AREA - bfa;
  yDraw = DISPLAY_HEIGHT - bfa - TEXT_HEIGHT;
  setupScrollArea(TOP_FIXED_AREA, bfa);

  for (byte i = 0; i < 19; i++) blank[i] = 0;

  terminalUpdateNavLabels();
}

void terminalLoop() {

  updateStatusBar();
  if (featureHasTouchNavBar()) {
    maintainTouchNavBar();
  }
  terminalHandleNavButtons();
  runUI();

  if (terminalActive) {
    byte charCount = 0;
    while (Serial.available() && charCount < 10) {
      data = Serial.read();
      if (data == '\r' || xPos > 231) {
        xPos = 0;
        yDraw = scroll_line();
      }
      if (data > 31 && data < 128) {
        xPos += tft.drawChar(data, xPos, yDraw, 2);
        blank[(18 + (yStart - TOP_FIXED_AREA) / TEXT_HEIGHT) % 19] = xPos;
      }
      charCount++;
      }
    }
  }
}

namespace AppSettingsUI {
static constexpr int top = 46, rowH = 29, count = 7;
static const char* labels[count] = {
  "Brightness", "Theme", "Accent", "Status LED", "Background scan", "Detection sound", "Test sound"
};
static int selected = 0;
static bool dirty = false, redraw = false, touchArmed = false, opening = true;
static bool wasDown[5] = {};
static char message[40] = "Tap a row or use the keys";

static const char* themeName(Theme theme) {
  switch (theme) {
    case Theme::Light: return "Light";
    case Theme::Hacker: return "Green";
    case Theme::Shark: return "Mono";
    default: return "Dark";
  }
}
static void hint(const char* text) {
  snprintf(message, sizeof(message), "%s", text);
  redraw = true;
}
static void draw() {
  applyThemeToPalette(settings().theme);
  tft.fillScreen(UI.bg);
  drawStatusBar(currentBatteryVoltage, true);
  tft.setTextSize(1); tft.setTextFont(2); tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI.text, UI.bg);
  tft.drawString("Settings", 10, 25, 2);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("Back", tft.width() - 10, 25, 2);
  const auto& s = settings();
  for (int i = 0; i < count; ++i) {
    const int y = top + i * rowH;
    const uint16_t bg = i == selected ? UI.icon : UI.bg;
    const uint16_t fg = i == selected ? TFT_BLACK : UI.text;
    tft.fillRect(6, y, tft.width() - 12, rowH - 1, bg);
    tft.drawFastHLine(6, y + rowH - 1, tft.width() - 12, UI.line);
    tft.setTextColor(fg, bg); tft.setTextDatum(ML_DATUM);
    tft.drawString(labels[i], 12, y + rowH / 2, 2);
    char value[20];
    switch (i) {
      case 0: snprintf(value, sizeof(value), "%u%%", (unsigned)((s.brightness * 100u + 127u) / 255u)); break;
      case 1: snprintf(value, sizeof(value), "%s", themeName(s.theme)); break;
      case 2: snprintf(value, sizeof(value), "%s", accentPresetName(s.accentColor)); break;
      case 3: snprintf(value, sizeof(value), "%s", s.neopixelEnabled ? "On" : "Off"); break;
      case 4: snprintf(value, sizeof(value), "%s", s.autoWifiScan || s.autoBleScan ? "On" : "Off"); break;
      case 5: snprintf(value, sizeof(value), "%s", !DetectionAlerts::available() ? "No buzzer" : s.detectionSound ? "On" : "Muted"); break;
      default: snprintf(value, sizeof(value), "%s", s.detectionSound ? "Play" : "Muted"); break;
    }
    tft.setTextDatum(MR_DATUM);
    tft.drawString(value, tft.width() - 12, y + rowH / 2, 2);
  }
  tft.setTextFont(1); tft.setTextDatum(TL_DATUM); tft.setTextColor(UI_DIM_TEXT, UI.bg);
  tft.drawString(message, 10, top + count * rowH + 10, 1);
  setTouchNavLabels("-", "Down", "Save", "Up", "+");
  redrawTouchButtonBar();
  redraw = false;
}
static void leave() {
  setTouchButtonInputEnabled(false);
  feature_exit_requested = true;
}
static void saveAndLeave() {
  if (!dirty || settingsSave()) { dirty = false; leave(); }
  else hint("SD save failed; tap Back to leave");
}
static void change(int direction) {
  auto& s = settings();
  switch (selected) {
    case 0: {
      // Never let the settings screen extinguish its own backlight.
      int value = (int)s.brightness + direction * 16;
      if (value < 16) value = 16;
      if (value > 255) value = 255;
      s.brightness = (uint8_t)value; ::setBrightness(s.brightness); break;
    }
    case 1: s.theme = (Theme)((uint8_t(s.theme) + kThemeCount + direction) % kThemeCount); break;
    case 2: s.accentColor = (s.accentColor + ACCENT_PRESET_COUNT + direction) % ACCENT_PRESET_COUNT; break;
    case 3: s.neopixelEnabled = !s.neopixelEnabled; break;
    case 4: s.autoWifiScan = s.autoBleScan = !(s.autoWifiScan || s.autoBleScan); break;
    case 5: {
      const bool value = !s.detectionSound;
      const bool saved = settingsSetDetectionSound(value);
      DetectionAlerts::setEnabled(value);
      hint(saved ? (value ? "Sound on; saved on device" : "Muted; saved on device") : "Sound changed; device save failed");
      dirty = true; return;
    }
    case 6:
      hint(DetectionAlerts::preview() ? "Playing two short chirps" : "Enable sound to test the buzzer");
      return;
  }
  dirty = true;
  hint("Changed - Save to keep settings");
}
void setup() {
  selected = 0; dirty = false; redraw = true; touchArmed = false; opening = true;
  for (bool& down : wasDown) down = false;
  snprintf(message, sizeof(message), "Tap a row or use the keys");
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("-", "Down", "Save", "Up", "+");
  draw();
}
void loop() {
  const int pins[5] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_SELECT};
  bool down[5], any = false;
  for (int i = 0; i < 5; ++i) { down[i] = isButtonPressed(pins[i]); any |= down[i]; }
  int tx = 0, ty = 0;
  const bool touching = readTouchXY(tx, ty);
  // Do not consume the press that opened Settings as a Save/Back press.
  if (opening) {
    for (int i = 0; i < 5; ++i) wasDown[i] = down[i];
    if (!any && !touching) { opening = false; touchArmed = true; }
    delay(5); return;
  }
  if (down[0] && !wasDown[0]) { selected = (selected + count - 1) % count; redraw = true; }
  if (down[1] && !wasDown[1]) { selected = (selected + 1) % count; redraw = true; }
  if (down[2] && !wasDown[2]) change(-1);
  if (down[3] && !wasDown[3]) change(1);
  if (down[4] && !wasDown[4]) { saveAndLeave(); if (feature_exit_requested) return; }
  for (int i = 0; i < 5; ++i) wasDown[i] = down[i];
  if (!touching) touchArmed = true;
  else if (touchArmed) {
    touchArmed = false;
    if (ty >= 22 && ty < top && tx >= tft.width() - 68) { leave(); return; }
    if (tx >= 6 && tx < tft.width() - 6 && ty >= top && ty < top + count * rowH) {
      selected = (ty - top) / rowH;
      change(1);
    }
  }
  if (redraw) draw();
  delay(5);
}
}

namespace SdFileManager {

using FeatureUI::Button;
using FeatureUI::ButtonStyle;

static constexpr uint16_t COL_BG   = FEATURE_BG;
static constexpr int STATUS_BAR_H = 20;
static constexpr int HEADER_H = 54;

static String ellipsize(const String& s, int maxW, uint8_t font) {
  if (tft.textWidth(s, font) <= maxW) return s;
  String out = s;
  while (out.length() > 0 && tft.textWidth(out + "...", font) > maxW) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

struct Entry {
  String name;
  String path;
  bool isDir = false;
  uint32_t size = 0;
  bool isUp = false; // synthetic ".."
};

enum class Page : uint8_t { Browser, Info, ConfirmDelete };

static Page page = Page::Browser;
static std::vector<Entry> entries;
static String cwd = "/";
static int sel = 0;
static int listStart = 0;
static bool uiDirty = true;
static bool touchActive = false;
static bool touchDragging = false;
static int touchStartX = 0;
static int touchStartY = 0;
static int touchLastY = 0;
static int scrollAccumY = 0;

static Button browserBtns[4];
static Button infoBtns[2];
static Button confirmBtns[2];

static String lastErr;

static int sdFmContentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY()
                                 : (int)(tft.height() - FeatureUI::FOOTER_H - 2);
}

static void sdFmRestoreNavChrome() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  invalidateTouchButtonCue();
  maintainTouchNavBar();
}

static char s_sdFmNavCache[5][12] = {{0}};

static void sdFmResetNavLabelCache() {
  for (int i = 0; i < 5; ++i) {
    s_sdFmNavCache[i][0] = '\0';
  }
}

static void sdFmApplyNavLabels(const char* left, const char* down, const char* center,
                               const char* up, const char* right) {
  if (!featureHasTouchNavBar()) {
    return;
  }
  const char* src[5] = {left, down, center, up, right};
  bool same = true;
  for (int i = 0; i < 5; ++i) {
    const char* s = (src[i] && src[i][0]) ? src[i] : "";
    if (strcmp(s_sdFmNavCache[i], s) != 0) {
      same = false;
      break;
    }
  }
  if (same) {
    return;
  }
  for (int i = 0; i < 5; ++i) {
    const char* s = (src[i] && src[i][0]) ? src[i] : "";
    strncpy(s_sdFmNavCache[i], s, sizeof(s_sdFmNavCache[i]) - 1);
    s_sdFmNavCache[i][sizeof(s_sdFmNavCache[i]) - 1] = '\0';
  }
  setTouchNavLabels(left, down, center, up, right);
  redrawTouchButtonBar();
}

static void sdFmUpdateBrowserNavLabels() {
  sdFmApplyNavLabels("Exit", "Next", "Refresh", "Prev", "Open");
}

static void sdFmUpdateInfoNavLabels() {
  sdFmApplyNavLabels("Back", nullptr, "Delete", nullptr, nullptr);
}

static void sdFmUpdateConfirmNavLabels() {
  sdFmApplyNavLabels("Cancel", nullptr, "Delete", nullptr, nullptr);
}

static void clampSel();
static bool reloadDir(const String& path, String* errOut);
static void resetListDrawCache();
static void drawBrowserPage(bool full = true);
static void drawConfirmDeletePage();
static void openSelected();
static bool deleteSelected(String* errOut);

static void sdFmHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (page == Page::Browser) {
    if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
      feature_exit_requested = true;
      return;
    }
    if (isTouchNavButtonPressedEdge(BTN_UP) && !entries.empty()) {
      sel--;
      clampSel();
      drawBrowserPage(false);
      return;
    }
    if (isTouchNavButtonPressedEdge(BTN_DOWN) && !entries.empty()) {
      sel++;
      clampSel();
      drawBrowserPage(false);
      return;
    }
    if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
      openSelected();
      return;
    }
    if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
      reloadDir(cwd, nullptr);
      drawBrowserPage(true);
    }
  } else if (page == Page::Info) {
    if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
      drawBrowserPage();
      return;
    }
    if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
      drawConfirmDeletePage();
    }
  } else if (page == Page::ConfirmDelete) {
    if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
      drawBrowserPage();
      return;
    }
    if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
      String err;
      const bool ok = deleteSelected(&err);
      if (!ok) {
        showNotification("Delete", err.c_str());
        delay(500);
      }
      reloadDir(cwd, nullptr);
      drawBrowserPage();
    }
  }
}

static File sdOpenCompat(const String& path) {
  File f = SD.open(path.c_str());
  if (f) return f;
  if (path.length() > 1 && path[0] == '/') {
    f = SD.open(path.substring(1).c_str());
  }
  return f;
}

static bool sdRemoveCompat(const String& path) {
  if (SD.remove(path.c_str())) return true;
  if (path.length() > 1 && path[0] == '/') return SD.remove(path.substring(1).c_str());
  return false;
}

static bool sdRmdirCompat(const String& path) {
  if (SD.rmdir(path.c_str())) return true;
  if (path.length() > 1 && path[0] == '/') return SD.rmdir(path.substring(1).c_str());
  return false;
}

static String normalizePath(const String& path) {
  if (path.length() == 0) return "/";
  String p = path;
  if (p[0] != '/') p = "/" + p;

  String out;
  out.reserve(p.length());
  bool lastSlash = false;
  for (size_t i = 0; i < p.length(); ++i) {
    char c = p[i];
    if (c == '/') {
      if (!lastSlash) out += c;
      lastSlash = true;
    } else {
      out += c;
      lastSlash = false;
    }
  }

  while (out.length() > 1 && out.endsWith("/")) {
    out.remove(out.length() - 1);
  }
  return out;
}

static void clampSel() {
  if (entries.empty()) { sel = 0; return; }
  if (sel < 0) sel = 0;
  if (sel >= (int)entries.size()) sel = (int)entries.size() - 1;
}

static void listLayout(int& top, int& bottom, int& rowH, int& maxVisible) {
  top = STATUS_BAR_H + HEADER_H + 6;
  bottom = sdFmContentBottom();
  rowH = 22;
  maxVisible = max(1, (bottom - top) / rowH);
}

static void clampListStart(int maxVisible) {
  int maxStart = max(0, (int)entries.size() - maxVisible);
  if (listStart < 0) listStart = 0;
  if (listStart > maxStart) listStart = maxStart;
}

static void ensureSelectionVisible(int maxVisible) {
  if (entries.empty()) { listStart = 0; return; }
  if (sel < listStart) listStart = sel;
  if (sel >= listStart + maxVisible) listStart = sel - maxVisible + 1;
  clampListStart(maxVisible);
}

static String parentPath(const String& p) {
  String np = normalizePath(p);
  if (np == "/") return "/";
  int slash = np.lastIndexOf('/');
  if (slash <= 0) return "/";
  return np.substring(0, slash);
}

static bool reloadDir(const String& path, String* errOut = nullptr) {
  entries.clear();
  lastErr = "";

  if (!isSDCardAvailable()) {
    if (errOut) *errOut = "SD not mounted";
    lastErr = "SD not mounted";
    return false;
  }

  String openPath = normalizePath(path);

  File dir = sdOpenCompat(openPath);
  if (!dir) {
    if (errOut) *errOut = "Open dir failed";
    lastErr = "Open dir failed";
    return false;
  }
  if (!dir.isDirectory()) {
    dir.close();
    if (errOut) *errOut = "Not a directory";
    lastErr = "Not a directory";
    return false;
  }

  cwd = openPath;

  if (cwd != "/") {
    Entry up;
    up.name = "..";
    up.path = parentPath(cwd);
    up.isDir = true;
    up.isUp = true;
    entries.push_back(up);
  }

  for (;;) {
    File f = dir.openNextFile();
    if (!f) break;

    Entry e;
    {
      String raw = String(f.name());
      while (raw.endsWith("/")) raw.remove(raw.length() - 1);
      int slash = raw.lastIndexOf('/');
      String base = (slash >= 0) ? raw.substring(slash + 1) : raw;
      while (base.startsWith("/")) base.remove(0, 1);
      while (base.endsWith("/")) base.remove(base.length() - 1);
      e.name = base.length() ? base : raw;
      while (e.name.startsWith("/")) e.name.remove(0, 1);
      while (e.name.endsWith("/")) e.name.remove(e.name.length() - 1);
      if (e.name.length() == 0) { f.close(); continue; }
    }
    e.isDir = f.isDirectory();
    e.size = e.isDir ? 0 : (uint32_t)f.size();
    if (cwd == "/") e.path = "/" + e.name;
    else e.path = cwd + "/" + e.name;
    e.path = normalizePath(e.path);

    entries.push_back(e);
    f.close();
    yield();
  }
  dir.close();

  // Sort, keeping ".." first when present.
  int start = (entries.size() && entries[0].isUp) ? 1 : 0;
  std::sort(entries.begin() + start, entries.end(), [](const Entry& a, const Entry& b) {
    if (a.isDir != b.isDir) return a.isDir > b.isDir;
    return a.name < b.name;
  });

  sel = 0;
  listStart = 0;
  uiDirty = true;
  resetListDrawCache();
  return true;
}

static void drawHeader(const char* title) {
  const int w = tft.width();
  const int y0 = STATUS_BAR_H;
  tft.fillRect(0, y0, w, HEADER_H, FEATURE_BG);

  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(UI_ICON, FEATURE_BG);
  tft.setCursor(8, y0 + 4);
  tft.print(title);

  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  String pathLine = ellipsize(cwd, w - 16, 1);
  tft.setCursor(8, y0 + 24);
  tft.print(pathLine);

  String right = entries.empty()
    ? String("0/0")
    : String(sel + 1) + "/" + String((int)entries.size());
  String left = String("Items: ") + String((int)entries.size());
  int rightW = tft.textWidth(right, 1);
  tft.setCursor(8, y0 + 40);
  tft.print(left);
  tft.setCursor(w - 8 - rightW, y0 + 40);
  tft.print(right);

  tft.drawLine(0, y0 + HEADER_H, w, y0 + HEADER_H, UI_LINE);
}

static void drawHeaderSelCounter() {
  const int w = tft.width();
  const int y0 = STATUS_BAR_H;
  String right = entries.empty()
    ? String("0/0")
    : String(sel + 1) + "/" + String((int)entries.size());
  int rightW = tft.textWidth(right, 1);
  tft.fillRect(w - 8 - max(rightW, 40), y0 + 38, max(rightW, 40) + 4, 14, FEATURE_BG);
  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
  tft.setCursor(w - 8 - rightW, y0 + 40);
  tft.print(right);
}

static void drawListRow(int idx, int y, int rowH, bool selected) {
  const int x = 8;
  const int w = tft.width() - 16;
  uint16_t bg = selected ? UI_FG : FEATURE_BG;
  uint16_t fg = selected ? UI_ICON : UI_TEXT;

  tft.fillRect(x, y, w, rowH - 1, bg);
  tft.drawLine(x, y + rowH - 1, x + w, y + rowH - 1, UI_LINE);

  tft.setTextFont(1);
  tft.setTextColor(fg, bg);
  String label = entries[idx].name;
  if (entries[idx].isDir && !entries[idx].isUp) label = label + "/";
  label = ellipsize(label, w - 80, 1);
  tft.setCursor(x + 4, y + 6);
  tft.print(label);

  String right = entries[idx].isDir ? "DIR" : "";
  if (!entries[idx].isDir && !entries[idx].isUp) {
    char sbuf[16];
    uint32_t kb = (entries[idx].size + 1023) / 1024;
    snprintf(sbuf, sizeof(sbuf), "%lukB", (unsigned long)kb);
    right = sbuf;
  }
  if (right.length()) {
    int tw = tft.textWidth(right, 1);
    tft.setCursor(x + w - tw - 6, y + 6);
    tft.print(right);
  }
}

static void drawListScrollbar(int top, int bottom, int start, int maxVisible) {
  if ((int)entries.size() <= maxVisible) {
    return;
  }
  int scrollBarX = tft.width() - 6;
  int scrollBarHeight = bottom - top - 6;
  int scrollBarY = top + 3;
  int indicatorHeight = (maxVisible * scrollBarHeight) / (int)entries.size();
  if (indicatorHeight < 6) indicatorHeight = 6;
  int indicatorY = scrollBarY;
  indicatorY = scrollBarY + (start * (scrollBarHeight - indicatorHeight)) /
                            max(1, (int)entries.size() - maxVisible);
  tft.fillRect(scrollBarX, scrollBarY, 3, scrollBarHeight, UI_LINE);
  tft.fillRect(scrollBarX, indicatorY, 3, indicatorHeight, UI_ACCENT);
}

static int s_lastListSel = -1;
static int s_lastListStart = -1;

static void resetListDrawCache() {
  s_lastListSel = -1;
  s_lastListStart = -1;
}

static void drawList(bool full = true) {
  int top, bottom, rowH, maxVisible;
  listLayout(top, bottom, rowH, maxVisible);

  if (!isSDCardAvailable()) {
    tft.fillRect(0, top, tft.width(), bottom - top, COL_BG);
    tft.setTextFont(2);
    tft.setTextColor(UI_WARN, COL_BG);
    tft.drawCentreString("SD not mounted", tft.width() / 2, top + 30, 2);
    resetListDrawCache();
    return;
  }

  if (entries.empty()) {
    tft.fillRect(0, top, tft.width(), bottom - top, COL_BG);
    tft.setTextFont(2);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString("Empty folder", tft.width() / 2, top + 30, 2);
    resetListDrawCache();
    return;
  }

  clampSel();
  ensureSelectionVisible(maxVisible);

  int start = listStart;
  int end = min((int)entries.size(), start + maxVisible);

  const bool windowChanged = full || s_lastListSel < 0 || s_lastListStart != start;
  if (windowChanged) {
    tft.fillRect(0, top, tft.width(), bottom - top, COL_BG);
    for (int i = start; i < end; ++i) {
      int y = top + (i - start) * rowH;
      drawListRow(i, y, rowH, i == sel);
    }
    drawListScrollbar(top, bottom, start, maxVisible);
    drawHeaderSelCounter();
    s_lastListSel = sel;
    s_lastListStart = start;
    return;
  }

  if (s_lastListSel != sel) {
    drawHeaderSelCounter();
    const int oldSel = s_lastListSel;
    const int newSel = sel;
    if (oldSel >= start && oldSel < end) {
      drawListRow(oldSel, top + (oldSel - start) * rowH, rowH, false);
    }
    if (newSel >= start && newSel < end) {
      drawListRow(newSel, top + (newSel - start) * rowH, rowH, true);
    }
    s_lastListSel = sel;
  }
}

static void drawBrowserFooter() {
  if (featureHasTouchNavBar()) {
    sdFmUpdateBrowserNavLabels();
    return;
  }
  bool sdOk = isSDCardAvailable();
  bool hasSel = !entries.empty();
  bool selIsUp = false;
  if (hasSel) {
    clampSel();
    selIsUp = entries[sel].isUp;
  }

  FeatureUI::drawFooterBg();
  FeatureUI::layoutFooter4(
    browserBtns,
    "Exit",    ButtonStyle::Secondary,
    "Open",    ButtonStyle::Primary,
    "Delete",  ButtonStyle::Danger,
    "Refresh", ButtonStyle::Secondary,
    false,
    (!sdOk || !hasSel),
    (!sdOk || !hasSel || selIsUp),
    false
  );
  for (auto& b : browserBtns) FeatureUI::drawButton(b);
}

static void drawBrowserPage(bool full) {
  page = Page::Browser;
  if (full) {
    featureClearContent(COL_BG);
    currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    drawHeader("SD File Manager");
    drawBrowserFooter();
    drawList(true);
    sdFmRestoreNavChrome();
    uiDirty = false;
    return;
  }

  drawList(false);
  uiDirty = false;
}

static void drawInfoPage() {
  page = Page::Info;
  featureClearContent(COL_BG);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  drawHeader("File info");

  if (featureHasTouchNavBar()) {
    sdFmUpdateInfoNavLabels();
  } else {
    FeatureUI::drawFooterBg();
    FeatureUI::layoutFooter2(
      infoBtns,
      "Back",   ButtonStyle::Secondary,
      "Delete", ButtonStyle::Danger,
      false, (entries.empty() || !isSDCardAvailable())
    );
    for (auto& b : infoBtns) FeatureUI::drawButton(b);
  }

  const int top = STATUS_BAR_H + HEADER_H + 6;
  const int bottom = sdFmContentBottom();
  tft.fillRect(0, top, tft.width(), bottom - top, COL_BG);

  if (entries.empty()) {
    tft.setTextFont(2);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString("No selection", tft.width()/2, top + 24, 2);
    uiDirty = false;
    return;
  }

  clampSel();
  const Entry& e = entries[sel];

  tft.setTextFont(2);
  tft.setTextColor(UI_TEXT, COL_BG);
  tft.setCursor(8, top + 10);
  tft.print("Name:");
  tft.setCursor(70, top + 10);
  tft.print(e.name);

  tft.setCursor(8, top + 34);
  tft.print("Type:");
  tft.setCursor(70, top + 34);
  tft.print(e.isDir ? "Folder" : "File");

  if (!e.isDir) {
    tft.setCursor(8, top + 58);
    tft.print("Size:");
    tft.setCursor(70, top + 58);
    tft.print(String(e.size) + " B");
  }

  tft.setTextFont(1);
  tft.setCursor(8, top + 88);
  tft.print("Path:");
  tft.setCursor(8, top + 102);
  tft.print(e.path);

  sdFmRestoreNavChrome();
  uiDirty = false;
}

static void drawConfirmDeletePage() {
  page = Page::ConfirmDelete;
  featureClearContent(COL_BG);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  drawHeader("Confirm delete");

  if (featureHasTouchNavBar()) {
    sdFmUpdateConfirmNavLabels();
  } else {
    FeatureUI::drawFooterBg();
    FeatureUI::layoutFooter2(
      confirmBtns,
      "Cancel", ButtonStyle::Secondary,
      "Delete", ButtonStyle::Danger,
      false, false
    );
    for (auto& b : confirmBtns) FeatureUI::drawButton(b);
  }

  const int top = STATUS_BAR_H + HEADER_H + 6;
  const int bottom = sdFmContentBottom();
  tft.fillRect(0, top, tft.width(), bottom - top, COL_BG);

  tft.setTextFont(2);
  tft.setTextColor(UI_WARN, COL_BG);
  tft.drawCentreString("Delete selected item?", tft.width()/2, top + 18, 2);

  if (!entries.empty()) {
    const Entry& e = entries[sel];
    tft.setTextFont(1);
    tft.setTextColor(UI_TEXT, COL_BG);
    tft.drawCentreString(e.name.c_str(), tft.width()/2, top + 48, 1);
  }

  sdFmRestoreNavChrome();
  uiDirty = false;
}

static bool deleteSelected(String* errOut = nullptr) {
  if (entries.empty()) { if (errOut) *errOut = "Nothing selected"; return false; }
  clampSel();
  const Entry e = entries[sel];
  if (e.isUp) { if (errOut) *errOut = "Cannot delete .."; return false; }

  if (!isSDCardAvailable()) { if (errOut) *errOut = "SD not mounted"; return false; }

  bool ok = false;
  if (e.isDir) {
    // ESP32 FS supports rmdir. If directory is not empty it will fail.
    ok = sdRmdirCompat(e.path);
  } else {
    ok = sdRemoveCompat(e.path);
  }
  if (!ok) {
    if (errOut) *errOut = "Delete failed";
    return false;
  }
  return true;
}

static void openSelected() {
  if (entries.empty()) return;
  clampSel();
  const Entry& e = entries[sel];

  if (e.isDir) {
    String err;
    if (!reloadDir(normalizePath(e.path), &err)) {
      drawBrowserPage();
      String msg = err.length() ? err : "Failed";
      msg += "\n";
      msg += e.path;
      showNotification("Open", msg.c_str());
      delay(600);
      return;
    }
    drawBrowserPage();
    return;
  }
  drawInfoPage();
}

static bool hitListRow(int x, int y, int& outIdx) {
  int top, bottom, rowH, maxVisible;
  listLayout(top, bottom, rowH, maxVisible);
  if (y < top || y >= bottom) return false;

  ensureSelectionVisible(maxVisible);
  int start = listStart;
  int end = min((int)entries.size(), start + maxVisible);

  int row = (y - top) / rowH;
  int idx = start + row;
  if (idx < start || idx >= end) return false;
  outIdx = idx;
  return true;
}

static void handleTap(int x, int y) {
  if (page == Page::Browser) {
    int idx = -1;
    if (hitListRow(x, y, idx)) {
      if (idx != sel) {
        sel = idx;
        clampSel();
        drawBrowserPage(false);
      } else {
        openSelected();
      }
      delay(140);
      return;
    }

    int f = -1;
    if (!featureHasTouchNavBar()) {
      f = FeatureUI::hit(browserBtns, 4, x, y);
    }
    if (f >= 0) {
      FeatureUI::drawButton(browserBtns[f], true);
      delay(90);
      FeatureUI::drawButton(browserBtns[f], false);
      delay(90);

      if (f == 0) { feature_exit_requested = true; return; }          // Exit
      if (f == 1) { openSelected(); delay(120); return; }             // Open
      if (f == 2) {                                                   // Delete
        if (!entries.empty() && isSDCardAvailable() && !entries[sel].isUp) {
          drawConfirmDeletePage();
        }
        delay(120);
        return;
      }
      if (f == 3) { reloadDir(cwd, nullptr); drawBrowserPage(true); delay(120); return; } // Refresh
    }
  }
  else if (page == Page::Info) {
    int f = -1;
    if (!featureHasTouchNavBar()) {
      f = FeatureUI::hit(infoBtns, 2, x, y);
    }
    if (f >= 0) {
      FeatureUI::drawButton(infoBtns[f], true);
      delay(90);
      FeatureUI::drawButton(infoBtns[f], false);
      delay(90);

      if (f == 0) { drawBrowserPage(); delay(120); return; }         // Back
      if (f == 1) { drawConfirmDeletePage(); delay(120); return; }   // Delete
    }
  }
  else if (page == Page::ConfirmDelete) {
    int f = -1;
    if (!featureHasTouchNavBar()) {
      f = FeatureUI::hit(confirmBtns, 2, x, y);
    }
    if (f >= 0) {
      FeatureUI::drawButton(confirmBtns[f], true);
      delay(90);
      FeatureUI::drawButton(confirmBtns[f], false);
      delay(90);

      if (f == 0) {                                                  // Cancel
        drawBrowserPage();
        delay(120);
        return;
      }
      if (f == 1) {                                                  // Delete
        String err;
        bool ok = deleteSelected(&err);
        if (!ok) {
          showNotification("Delete", err.c_str());
          delay(500);
        }
        reloadDir(cwd, nullptr);
        drawBrowserPage();
        delay(120);
        return;
      }
    }
  }
}

void setup() {
  sdRetryMount();
  sdFmResetNavLabelCache();
  page = Page::Browser;
  cwd = "/";
  sel = 0;
  uiDirty = true;
  reloadDir("/", nullptr);
  currentBatteryVoltage = readBatteryVoltage();
  drawStatusBar(currentBatteryVoltage, true);
  drawBrowserPage();
}

void loop() {
  updateStatusBar();
  if (featureHasTouchNavBar()) {
    maintainTouchNavBar();
  }
  sdFmHandleNavButtons();
  if (feature_exit_requested) {
    return;
  }
  if (uiDirty) {
    switch (page) {
      case Page::Browser:        drawBrowserPage(true); break;
      case Page::Info:           drawInfoPage(); break;
      case Page::ConfirmDelete:  drawConfirmDeletePage(); break;
    }
  }

  // Physical navigation (kept for devices without touch nav bar).
  if (!featureHasTouchNavBar() && isButtonPressed(BTN_UP)) {
    if (page == Page::Browser && !entries.empty()) { sel--; clampSel(); drawBrowserPage(false); }
    delay(160);
    return;
  }
  if (!featureHasTouchNavBar() && isButtonPressed(BTN_DOWN)) {
    if (page == Page::Browser && !entries.empty()) { sel++; clampSel(); drawBrowserPage(false); }
    delay(160);
    return;
  }

  int x, y;
  bool touched = readTouchXY(x, y);
  if (!touched) {
    if (touchActive && !touchDragging) {
      handleTap(touchStartX, touchStartY);
    }
    touchActive = false;
    touchDragging = false;
    scrollAccumY = 0;
    return;
  }

  if (!touchActive) {
    touchActive = true;
    touchDragging = false;
    touchStartX = x;
    touchStartY = y;
    touchLastY = y;
    scrollAccumY = 0;
    return;
  }

  if (page == Page::Browser) {
    int top, bottom, rowH, maxVisible;
    listLayout(top, bottom, rowH, maxVisible);
    bool canScroll = (touchStartY >= top && touchStartY < bottom);

    int dy = y - touchLastY;
    touchLastY = y;

    if (!touchDragging && canScroll) {
      if (abs(y - touchStartY) > 8) {
        touchDragging = true;
      }
    }

    if (touchDragging && canScroll) {
      scrollAccumY += dy;
      int steps = scrollAccumY / rowH;
      if (steps != 0) {
        listStart -= steps;
        clampListStart(maxVisible);
        if (!entries.empty()) {
          if (sel < listStart) sel = listStart;
          if (sel >= listStart + maxVisible) sel = min((int)entries.size() - 1, listStart + maxVisible - 1);
        }
        drawBrowserPage(false);
        scrollAccumY -= steps * rowH;
      }
    }
  }

  return;
}

} // namespace SdFileManager

namespace TouchCalib {
static int stepIdx = 0;
static uint16_t xs[4], ys[4];
static const int pts[4][2] = { {20,20}, {TFT_WIDTH-20,20}, {TFT_WIDTH-20,TFT_HEIGHT-20}, {20,TFT_HEIGHT-20} };

static void drawTarget(int x,int y){
  tft.fillScreen(UI_BG);
  tft.drawCircle(x,y,10,UI_ICON);
  tft.drawLine(x-14,y, x+14,y, UI_ICON);
  tft.drawLine(x,y-14, x,y+14, UI_ICON);
  tft.setCursor(70, 8);
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.print("Touch the target");
}

void setup(){
  stepIdx=0;
  drawTarget(pts[0][0], pts[0][1]);
}

void loop(){
  //校准值与实际不对应，一旦操作错误需电脑删除配置文件，暂时放弃保存结果
  // bool ok = settingsSave();
  if (stepIdx>=4){
    uint16_t xMin = min(xs[0], xs[3]);
    uint16_t xMax = max(xs[1], xs[2]);
    uint16_t yMin = min(ys[0], ys[1]);
    uint16_t yMax = max(ys[2], ys[3]);
    auto& s = settings();
    // s.touchXMin = xMin; s.touchXMax = xMax;
#if defined(BOARD_CYD)
    s.touchYMin = yMin; s.touchYMax = yMax;
#else
    // s.touchYMin = yMax; s.touchYMax = yMin;
#endif


    bool ok = false;

    tft.fillScreen(UI_BG);
    tft.setTextColor(ok ? UI.ok : UI.warn, UI_BG);
    tft.setCursor(70, 8);
    tft.print(ok ? "Calibration Saved" : "Save FAILED");

    tft.setTextColor(UI_TEXT, UI_BG);
    tft.setCursor(70,28);
    tft.printf("X:[%u..%u] Y:[%u..%u]", xMin,xMax,yMin,yMax);

    delay(1200);
    feature_exit_requested = true;
    return;
  }

  int16_t rx = 0, ry = 0;
  if (readTouchRawXY(rx, ry)) {
    xs[stepIdx] = (uint16_t)rx;
    ys[stepIdx] = (uint16_t)ry;
    stepIdx++;
    if (stepIdx<4) drawTarget(pts[stepIdx][0], pts[stepIdx][1]);
  }
  delay(100);
}
}
