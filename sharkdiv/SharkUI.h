#pragma once

// Shared display chrome. Geometry and content stay with each menu caller.
// Selected cells use UI.icon so existing black text/icons remain readable.
// Resting cells are flat with a single divider; startup shows product/version.
#include "shared.h"

void playSharkBoot();
void drawSharkTile(int16_t x, int16_t y, int16_t w, int16_t h, bool selected);
void drawSharkRowRail(int16_t y, int16_t h, bool selected);
void drawBitmap2x(int16_t x, int16_t y, const uint8_t* bmp, uint16_t color);
// Legacy name retained for callers; now clears a plain menu background.
void drawBrandGrid(int16_t y0, int16_t y1);
