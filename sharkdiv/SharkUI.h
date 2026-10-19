#pragma once

// SharkUI — M5SHARK-style UI layer for SHARKDIV.
//   playSharkBoot()   animated ctOS boot splash (character-cell skull that
//                     decodes out of carrier noise, glitch, lock)
//   drawSharkTile()   ctOS menu tile: hard edges, accent rail + corner ticks
//                     on the selected tile
//   drawSharkRowRail() list-row accent rail used by the submenus
// Ported from github.com/M5Shark/M5Shark (SharkUI/SharkTheme), restyled onto
// the ESP32-DIV runtime palette so every theme tints it.

#include "shared.h"

void playSharkBoot();
void drawSharkTile(int16_t x, int16_t y, int16_t w, int16_t h, bool selected);
void drawSharkRowRail(int16_t y, int16_t h, bool selected);
