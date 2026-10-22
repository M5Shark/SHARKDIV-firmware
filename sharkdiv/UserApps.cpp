// UserApps.cpp — registry + the demo app. Copy DemoApp as the template for
// new apps; keep loops non-blocking (return within ~50 ms) and poll
// featureExitButtonPressed() so Exit always works.

#include "UserApps.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"
#include "flock_menu.h"
// === generated includes (app builder writes below this line) ===

static const UserApp kUserApps[] = {
  SHARKDIV_APP_LIST
};
const int kUserAppCount = sizeof(kUserApps) / sizeof(kUserApps[0]);

const UserApp* userApps() { return kUserApps; }
int userAppCount() { return kUserAppCount; }

namespace DemoApp {

static uint32_t s_ticks = 0;
static uint32_t s_lastPaint = 0;

void appSetup() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, "Reset", "Exit", "Count", nullptr);
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("// DEMO APP", 6, 28, 2);
  tft.drawFastHLine(0, 38, tft.width(), UI_LINE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("template app - UP counts, DOWN resets", 8, 46, 1);
  setupTouchscreen();
  redrawTouchButtonBar();
  s_ticks = 0;
  s_lastPaint = 0;
}

void appLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_UP) || isButtonPressed(BTN_UP)) {
    s_ticks++;
  } else if (isTouchNavButtonPressedEdge(BTN_DOWN) || isButtonPressed(BTN_DOWN)) {
    s_ticks = 0;
  }
  if (millis() - s_lastPaint > 300) {
    s_lastPaint = millis();
    tft.setTextDatum(MC_DATUM);
    tft.fillRect(0, 120, tft.width(), 60, TFT_BLACK);
    tft.setTextColor(UI_ICON, TFT_BLACK);
    tft.drawString(String(s_ticks), tft.width() / 2, 140, 4);
    tft.setTextDatum(TL_DATUM);
  }
  delay(15);
}

void exit() {}

}  // namespace DemoApp
