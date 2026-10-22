#pragma once

// SharkUI — SHARKDIV UI layer.
//   playSharkBoot()   1-bit glitch boot reel (wipe, the user's skull
//                     dropping in and dissolving, the shark mark pulling back
//                     from its eye, dot-matrix fade) followed by the logo splash
//   drawSharkTile()   menu card: glass surface, solid accent when selected
//   drawSharkRowRail() list-row surface used by the submenus
//   drawBitmap2x()    icon.h bitmap at double size
//   drawBrandGrid()   faint dot grid behind the menus (brand theme)

#include "shared.h"

void playSharkBoot();
void drawSharkTile(int16_t x, int16_t y, int16_t w, int16_t h, bool selected);
void drawSharkRowRail(int16_t y, int16_t h, bool selected);
void drawBitmap2x(int16_t x, int16_t y, const uint8_t* bmp, uint16_t color);
void drawBrandGrid(int16_t y0, int16_t y1);
