// SHARKDIV display chrome: flat instrument cells and a short static boot mark.
#include "SharkUI.h"
#include "SharkLogo.h"
#include "config.h"
#include "SettingsStore.h"

// Callers own text/icons and hit targets. Keep the full selected surface bright
// because their selected foreground is black; resting cells use only a divider.
void drawSharkTile(int16_t x, int16_t y, int16_t w, int16_t h, bool selected) {
  if (w <= 0 || h <= 0) return;
  tft.startWrite();
  tft.fillRect(x, y, w, h, selected ? UI.icon : UI.bg);
  if (!selected) tft.drawFastHLine(x, y + h - 1, w, UI.line);
  tft.endWrite();
}

void drawSharkRowRail(int16_t y, int16_t h, bool selected) {
  // Preserve the six-pixel inset and row height used by menu touch handling.
  const int16_t inset = 6;
  drawSharkTile(inset, y, tft.width() - 2 * inset, h, selected);
}

// Retained for screens that need a 32 px version of an existing 16 px icon.
void drawBitmap2x(int16_t x, int16_t y, const uint8_t* bmp, uint16_t color) {
  if (!bmp) return;
  tft.startWrite();
  for (int16_t r = 0; r < 16; r++) {
    for (int16_t c = 0; c < 16; c++) {
      const uint8_t b = pgm_read_byte(&bmp[r * 2 + (c >> 3)]);
      if (b & (0x80 >> (c & 7))) tft.fillRect(x + c * 2, y + r * 2, 2, 2, color);
    }
  }
  tft.endWrite();
}

// Legacy entry point: callers clear this region before drawing menu content.
// A plain surface keeps the small display readable without background texture.
void drawBrandGrid(int16_t y0, int16_t y1) {
  if (y0 < 0) y0 = 0;
  if (y1 > tft.height()) y1 = tft.height();
  if (y1 > y0) tft.fillRect(0, y0, tft.width(), y1 - y0, UI.bg);
}

void playSharkBoot() {
  applyThemeToPalette(settings().theme);
  tft.fillScreen(UI.bg);

  // Draw directly to the panel: no full-screen allocation, flashing frames,
  // simulated progress or waiting for a multi-second animation to finish.
  const int16_t centerX = tft.width() / 2;
  const int16_t top = (tft.height() - 218) / 2;
  tft.drawBitmap(centerX - SHARK_LOGO_W / 2, top, shark_logo_bitmap,
                 SHARK_LOGO_W, SHARK_LOGO_H, UI.icon);

  tft.setTextSize(1);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextDatum(C_BASELINE);
  tft.setTextColor(UI.text);
  tft.drawString(ESP32DIV_NAME, centerX, top + 170);

  tft.setTextFont(1);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(UI_DIM_TEXT);
  tft.drawString("RADIO / SIGNAL / TOOLS", centerX, top + 182);
  tft.drawFastHLine(24, top + 199, tft.width() - 48, UI.line);
  tft.drawString(ESP32DIV_VERSION, centerX, top + 208);
  tft.setTextDatum(TL_DATUM);

  // Brief identity cue; hardware initialization continues after this function.
  delay(400);
}
