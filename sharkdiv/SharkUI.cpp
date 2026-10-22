// SharkUI.cpp — SHARKDIV boot splash and menu chrome.
// Boot: a 1-bit glitch reel (white wipe, rising blocks, the user's skull
// dropping in and dissolving, the shark mark pulling back from its eye, a
// dot-matrix fade) followed by the logo splash. Art: the user's "ASCII Skull
// Jaw" piece (SharkBootArt.h) and shark-head logo (SharkLogo.h).

#include "SharkUI.h"
#include "SharkLogo.h"
#include "SharkBootArt.h"
#include "config.h"
#include "SettingsStore.h"

// ── menu tile ──────────────────────────────────────────────────────────
void drawSharkTile(int16_t x, int16_t y, int16_t w, int16_t h, bool selected) {
  const int16_t r = 8;
  tft.startWrite();
  // Clear the bounding box first so the rounded-corner gaps never keep a stale
  // fill when the selection moves between tiles.
  tft.fillRect(x, y, w, h, UI_BG);
  if (selected) {
    // Focused tile: a solid accent card. The caller draws the icon and label
    // in ink on top, so the selection reads as a filled block — not a hairline.
    tft.fillRoundRect(x, y, w, h, r, UI.icon);
  } else {
    // Resting tile: an elevated dark surface with a hairline rounded border.
    tft.fillRoundRect(x, y, w, h, r, UI.fg);
    tft.drawRoundRect(x, y, w, h, r, UI.line);
  }
  tft.endWrite();
}

// List rows are full-width rounded surfaces: the focused row is a filled accent
// bar (caller draws its label in ink), resting rows a quiet dark surface. The
// band is cleared first so moving the selection never leaves a stale fill.
void drawSharkRowRail(int16_t y, int16_t h, bool selected) {
  const int16_t m = 6, r = 8;
  const int16_t w = tft.width() - m * 2;
  tft.startWrite();
  tft.fillRect(m, y, w, h, UI_BG);   // card area only: leaves the margin grid intact
  if (selected) {
    tft.fillRoundRect(m, y, w, h, r, UI.icon);
  } else {
    tft.fillRoundRect(m, y, w, h, r, UI.fg);
    tft.drawRoundRect(m, y, w, h, r, UI.line);
  }
  tft.endWrite();
}

// A 16x16 icon.h bitmap at double size — each set pixel becomes a 2x2 block,
// so home tiles get bold 32 px icons from the same assets.
void drawBitmap2x(int16_t x, int16_t y, const uint8_t* bmp, uint16_t color) {
  tft.startWrite();
  for (int16_t r = 0; r < 16; r++) {
    for (int16_t c = 0; c < 16; c++) {
      const uint8_t b = pgm_read_byte(&bmp[r * 2 + (c >> 3)]);
      if (b & (0x80 >> (c & 7))) tft.fillRect(x + c * 2, y + r * 2, 2, 2, color);
    }
  }
  tft.endWrite();
}

// Faint dot grid behind the menus (the flasher's background), brand theme only.
void drawBrandGrid(int16_t y0, int16_t y1) {
  if (UI.icon != BRAND_ACCENT) return;
  tft.startWrite();
  for (int16_t y = y0 + 4; y < y1; y += 12)
    for (int16_t x = 4; x < tft.width(); x += 12)
      tft.drawPixel(x, y, BRAND_GRID);
  tft.endWrite();
}

// ── boot splash ────────────────────────────────────────────────────────
// Every frame is drawn whole into a full-screen sprite and pushed, snapping
// from pose to pose with no tweening. About 3.5 s in all.
namespace {

constexpr int kW = 240, kH = 320;
constexpr uint16_t kWhite = 0xFFFF;
constexpr uint16_t kBlack = 0x0000;
constexpr uint16_t kGhost = 0x1AC6;    // dim green, outro skull
constexpr uint16_t kScan  = 0xCFFB;    // scan bar over the splash logo

const uint8_t kBayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };

// Deterministic hash so the "random" debris is the same on every boot.
uint32_t h32(uint32_t a, uint32_t b, uint32_t c = 0) {
  uint32_t x = a * 73856093u ^ b * 19349663u ^ c * 83492791u;
  x ^= x >> 13;
  x *= 0x5bd1e995u;
  x ^= x >> 15;
  return x;
}

inline bool bitAt(const uint8_t* bmp, int w, int x, int y) {
  return pgm_read_byte(&bmp[y * ((w + 7) >> 3) + (x >> 3)]) & (0x80 >> (x & 7));
}

// 16-bit sprites keep their pixels byte-swapped, ready to push.
inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

TFT_eSprite* S = nullptr;
uint16_t* buf = nullptr;
uint32_t frameStart = 0;

void frame(uint32_t ms) {
  S->pushSprite(0, 0);
  while (millis() - frameStart < ms) delay(1);
  frameStart = millis();
}

// Forward-map each set cell of a 1-bit mosaic: its centre is rotated about
// (cx, cy) and drawn as a square block. drop / scatter break the mosaic up.
void cells(const uint8_t* m, int mw, int mh, float cx, float cy, float cell, float angDeg,
           uint16_t col, int drop = 0, float scatter = 0, uint32_t seed = 0) {
  const float a = angDeg * DEG_TO_RAD, ca = cosf(a), sa = sinf(a);
  const int sz = (int)(cell + 0.99f);
  for (int iy = 0; iy < mh; iy++) {
    for (int ix = 0; ix < mw; ix++) {
      if (!bitAt(m, mw, ix, iy)) continue;
      const uint32_t hh = h32(ix, iy, seed);
      if (drop && (int)(hh % 100) < drop) continue;
      const float ox = (ix - mw / 2.0f + 0.5f) * cell, oy = (iy - mh / 2.0f + 0.5f) * cell;
      float x = cx + ox * ca - oy * sa, y = cy + ox * sa + oy * ca;
      if (scatter > 0) {
        x += ((int)((hh >> 8) % 41) - 20) * scatter / 20.0f;
        y += (int)((hh >> 16) % 31) * scatter / 10.0f;
      }
      S->fillRect((int)(x - cell / 2), (int)(y - cell / 2), sz, sz, col);
    }
  }
}

// Inverse-map the shark logo onto the whole screen: scale and rotate about
// screen point (cx, cy), which shows logo point (fx, fy). Nearest neighbour.
void logo(float scale, float angDeg, float cx, float cy, float fx, float fy, uint16_t col) {
  const float a = -angDeg * DEG_TO_RAD, ca = cosf(a) / scale, sa = sinf(a) / scale;
  const uint16_t c = swap16(col);
  for (int y = 0; y < kH; y++) {
    const float dy = y - cy;
    float u = fx + (-cx) * ca - dy * sa;
    float v = fy + (-cx) * sa + dy * ca;
    uint16_t* row = buf + y * kW;
    for (int x = 0; x < kW; x++, u += ca, v += sa) {
      if (u < 0 || v < 0) continue;
      const int iu = (int)u, iv = (int)v;
      if (iu < SHARK_LOGO_W && iv < SHARK_LOGO_H && bitAt(shark_logo_bitmap, SHARK_LOGO_W, iu, iv))
        row[x] = c;
    }
  }
}

void dim(int level) {      // ordered-dither black over the frame (0..16)
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++)
      if (kBayer[y & 3][x & 3] < level) buf[y * kW + x] = 0;
}

void dots() {              // dot matrix: black every third row and column
  for (int y = 0; y < kH; y++) {
    uint16_t* row = buf + y * kW;
    if (y % 3 == 2) { memset(row, 0, kW * 2); continue; }
    for (int x = 2; x < kW; x += 3) row[x] = 0;
  }
}

void glitchBlocks(uint32_t seed, uint16_t col) {
  for (int i = 0; i < 9; i++) {
    const uint32_t hh = h32(i, seed, 7);
    const int w = 6 + hh % 22, h = 3 + (hh >> 5) % 10;
    S->fillRect((i % 2 == 0) ? 0 : kW - w, (hh >> 11) % kH, w, h, col);
  }
}

// The logo splash: dot grid, the shark revealed under a scan bar, wordmark,
// model + version and a boot bar. Draws on the sprite or straight to the
// panel, so it still shows when there is no memory for the reel.
void splash(TFT_eSPI& g, float reveal, float prog) {
  const int W = g.width(), H = g.height();
  g.fillRect(0, 0, W, H, kBlack);
  for (int y = 4; y < H; y += 12)
    for (int x = 4; x < W; x += 12) g.drawPixel(x, y, BRAND_GRID);
  const int lx = (W - SHARK_LOGO_W) / 2, ly = 44;
  const int lh = (int)(SHARK_LOGO_H * reveal);
  for (int v = 0; v < lh; v++)
    for (int u = 0; u < SHARK_LOGO_W; u++)
      if (bitAt(shark_logo_bitmap, SHARK_LOGO_W, u, v)) g.drawPixel(lx + u, ly + v, BRAND_ACCENT);
  if (reveal < 1.0f) g.fillRect(lx, ly + lh, SHARK_LOGO_W, 2, kScan);

  g.setFreeFont(&FreeSansBold12pt7b);
  const int w1 = g.textWidth("SHARK"), w2 = g.textWidth("DIV");
  const int wx = (W - w1 - w2) / 2;
  g.setTextDatum(L_BASELINE);
  g.setTextColor(BRAND_INK);
  g.drawString("SHARK", wx, 214);
  g.setTextColor(BRAND_ACCENT);
  g.drawString("DIV", wx + w1, 214);

  g.setTextFont(1);
  g.setTextDatum(TC_DATUM);
  g.setTextColor(BRAND_MUTED);
  g.drawString("ESP32-S3 MULTITOOL", W / 2, 226);
  g.drawString(ESP32DIV_VERSION, W / 2, 238);

  g.setTextDatum(TL_DATUM);
  g.drawString("BOOTING", 40, 268);
  g.drawRect(40, 280, 160, 7, BRAND_LINE);
  g.fillRect(41, 282, (int)(158 * prog), 3, BRAND_ACCENT);
}

void playReel() {
  frameStart = millis();

  // f0: white wipe with a ragged left edge
  S->fillSprite(kBlack);
  for (int b = 0; b < kH; b += 3) {
    const int x0 = (b > 210 && b < 228) ? 0 : (int)(h32(b, 1) % 14) * 2 + ((b >= 45 && b < 60) ? 16 : 0);
    S->fillRect(x0, b, kW - x0, 3, kWhite);
  }
  S->fillRect(0, 92, 14, 14, kWhite);
  frame(70);

  // black blocks rise
  S->fillSprite(kWhite);
  for (int col = 0; col < kW; col += 8) {
    const uint32_t hh = h32(col, 5);
    const int hgt = 40 + hh % 120;
    if ((hh >> 9) % 3) S->fillRect(col, kH - hgt, 8 - (hh >> 12) % 3, hgt, kBlack);
  }
  S->fillRect(0, kH - 40, kW, 40, kBlack);
  frame(70);

  // The skull drops in, overshoots, settles with a small shake, then
  // dissolves in place.
  const struct { int16_t cy; int8_t a; uint8_t drop; uint8_t ms; } sk[9] = {
    {40, 0, 0, 50}, {126, 0, 0, 50}, {164, 0, 0, 60}, {150, 0, 0, 120},
    {150, -4, 0, 80}, {150, 3, 0, 80}, {150, 0, 35, 80}, {150, 0, 65, 80}, {150, 0, 90, 80} };
  for (int k = 0; k < 9; k++) {
    S->fillSprite(kBlack);
    cells(boot_skull_shaded, BOOT_SKULL_SHADED_W, BOOT_SKULL_SHADED_H, 120, sk[k].cy, 3.1f,
          sk[k].a, kWhite, sk[k].drop, 0, 11);
    frame(sk[k].ms);
  }

  // white cut to the shark
  S->fillSprite(kWhite);
  frame(50);

  // The shark pulls back from its eye to full view, overshoots and settles.
  const struct { float sc; int8_t a; float t; uint8_t ms; } zoom[9] = {
    {7.2f, 0, 0.00f, 60}, {5.0f, 0, 0.18f, 60}, {3.4f, -4, 0.40f, 60},
    {2.3f, 2, 0.62f, 60}, {1.55f, -1, 0.82f, 60}, {1.18f, 0, 0.95f, 70},
    {0.92f, 0, 1.00f, 70}, {1.04f, 0, 1.00f, 70}, {1.0f, 0, 1.00f, 90} };
  const float eyeX = 104, eyeY = 64, cenX = 75, cenY = 70;
  for (int zi = 0; zi < 9; zi++) {
    S->fillSprite(kBlack);
    logo(zoom[zi].sc, zoom[zi].a, 120, 140,
         eyeX + (cenX - eyeX) * zoom[zi].t, eyeY + (cenY - eyeY) * zoom[zi].t, BRAND_ACCENT);
    if (zi == 8) glitchBlocks(zi, kWhite);
    frame(zoom[zi].ms);
  }

  // dim dot-matrix outro, the ASCII skull showing through
  const int levels[3] = { 4, 7, 10 };
  const uint16_t ghost = swap16(kGhost);
  for (int k = 0; k < 3; k++) {
    S->fillSprite(kBlack);
    logo(1.0f, 0, 120, 140, cenX, cenY, BRAND_ACCENT);
    if (k >= 1) {
      for (int y = 0; y < BOOT_SKULL_ASCII_H; y++)
        for (int x = 0; x < BOOT_SKULL_ASCII_W; x++)
          if (bitAt(boot_skull_ascii, BOOT_SKULL_ASCII_W, x, y)) buf[(96 + y) * kW + 45 + x] = ghost;
    }
    dim(levels[k]);
    dots();
    frame(90);
  }

  // Logo splash
  const struct { float reveal, prog; uint16_t ms; } sp[6] = {
    {0.35f, 0, 60}, {0.7f, 0, 60}, {1, 0.1f, 250}, {1, 0.45f, 250}, {1, 0.8f, 250}, {1, 1, 250} };
  for (int k = 0; k < 6; k++) {
    splash(*S, sp[k].reveal, sp[k].prog);
    frame(sp[k].ms);
  }
}

}  // namespace

void playSharkBoot() {
  applyThemeToPalette(settings().theme);

  TFT_eSprite spr(&tft);
  spr.setColorDepth(16);
  const bool fits = tft.width() == kW && tft.height() == kH;
  if (fits && spr.createSprite(kW, kH) != nullptr) {
    S = &spr;
    buf = (uint16_t*)spr.getPointer();
    playReel();
    spr.deleteSprite();
    S = nullptr;
    buf = nullptr;
  } else {
    // No room for the frame buffer (or an unexpected panel size): the logo
    // splash alone, drawn straight to the panel.
    splash(tft, 1.0f, 1.0f);
    delay(1200);
  }
}
