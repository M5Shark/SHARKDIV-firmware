#pragma once
#include <stdint.h>

namespace BootAnimation {
constexpr uint16_t Width = 120;
constexpr uint16_t Height = 160;
constexpr uint16_t BufferBytes = Width * Height / 8;
constexpr uint8_t FrameCount = 16;
constexpr uint16_t FrameMs = 90;

// Packed MSB-first portrait frame. An out-of-range index produces black.
void renderFrame(uint8_t index, uint8_t* canvas);
// One finite intro, before background display/radio tasks start.
void play();
}
