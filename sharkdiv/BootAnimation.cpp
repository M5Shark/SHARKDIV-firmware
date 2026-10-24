#include "BootAnimation.h"
#include "SkullBootFrames.h"
#include "config.h"
#include <string.h>

namespace BootAnimation {
namespace {
void pixel(uint8_t* canvas, int16_t x, int16_t y) {
  if (x < 0 || y < 0 || x >= Width || y >= Height) return;
  canvas[y * (Width / 8) + x / 8] |= 0x80 >> (x & 7);
}
}

void renderFrame(uint8_t index, uint8_t* canvas) {
  if (!canvas) return;
  memset(canvas, 0, BufferBytes);
  // The last frame is a short black separation before the user's shark logo.
  if (index >= FrameCount - 1) return;
  static const uint8_t poses[FrameCount - 1] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 7
  };
  const uint8_t pose = poses[index];
  for (int16_t sy = 0; sy < 45; ++sy) {
    // Reveal the first pose in two cuts, without a full-screen white flash.
    if (index == 0 && sy >= 23) continue;
    int16_t slice = 0;
    if (index == 3 && sy >= 12 && sy < 20) slice = 6;
    if (index == 7 && sy >= 23 && sy < 29) slice = -8;
    for (int16_t sx = 20; sx < 80; ++sx) {
      if (!(pgm_read_byte(&skull_boot_frames[pose][sy * 10 + sx / 8]) &
            (0x80 >> (sx & 7)))) continue;
      if (index == 14 && ((sx + sy) & 1)) continue;
      const int16_t x = (sx - 20) * 2 + slice;
      const int16_t y = 35 + sy * 2;
      pixel(canvas, x, y);
      pixel(canvas, x + 1, y);
      pixel(canvas, x, y + 1);
      pixel(canvas, x + 1, y + 1);
    }
  }
}

void play() {
  // This portrait sequence targets the DIV display. Other geometries retain
  // the centered logo stage instead of streaming outside their address window.
  if (tft.width() != Width * 2 || tft.height() != Height * 2) return;
  // No heap allocation or PSRAM requirement. Each full frame is assembled
  // before sending it, avoiding a visible clear/redraw flash between poses.
  static uint8_t canvas[BufferBytes];
  static uint16_t row[Width * 2];
  for (uint8_t frame = 0; frame < FrameCount; ++frame) {
    const uint32_t started = millis();
    renderFrame(frame, canvas);
    tft.startWrite();
    tft.setAddrWindow(0, 0, Width * 2, Height * 2);
    for (uint16_t y = 0; y < Height; ++y) {
      for (uint16_t x = 0; x < Width; ++x) {
        const uint16_t color =
          canvas[y * (Width / 8) + x / 8] & (0x80 >> (x & 7)) ? 0xFFFF : 0;
        row[x * 2] = row[x * 2 + 1] = color;
      }
      tft.pushColors(row, Width * 2, true);
      tft.pushColors(row, Width * 2, true);
    }
    tft.endWrite();
    const uint32_t elapsed = millis() - started;
    // Release SPI and yield every frame, including when rendering runs late.
    delay(elapsed < FrameMs ? FrameMs - elapsed : 1);
  }
}
}
