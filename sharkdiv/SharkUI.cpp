// SharkUI.cpp — SHARKDIV boot splash and menu chrome.
// Boot: a 1-bit glitch reel (white wipe, tumbling hourglass, rising blocks,
// the user's skull swinging and shattering, the shark mark slamming in, a
// dot-matrix fade) followed by the logo splash. Art: the user's "ASCII Skull
// Jaw" piece (SharkBootArt.h) and shark-head logo (SharkLogo.h).

#include "SharkUI.h"
#include "SharkLogo.h"
#include "SharkBootArt.h"
#include "config.h"
#include "SettingsStore.h"

// ── ctOS menu tile ─────────────────────────────────────────────────────
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

void stepline(uint16_t col) {
  int x = 64;
  for (int y = 0; y < kH; y += 3) {
    S->fillRect(x, y, 3, 2, col);
    x += ((y / 3) % 3) ? 1 : 2;
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

  // f1-f4: white room, the hourglass tumbles, debris
  const struct { int8_t a; float sc; } hg[4] = { {-38, 5.0f}, {-66, 5.4f}, {-92, 5.7f}, {-112, 5.9f} };
  for (int k = 0; k < 4; k++) {
    S->fillSprite(kWhite);
    cells(boot_hourglass, BOOT_HOURGLASS_W, BOOT_HOURGLASS_H, 124, 150, hg[k].sc, hg[k].a, kBlack);
    if (k == 0 || k == 2) {
      for (int i = 0; i < 5; i++) {
        const uint32_t hh = h32(i, k, 3);
        S->fillRect(80 + hh % 90, 110 + (hh >> 8) % 90, 2 + hh % 4, 1 + (hh >> 4) % 2, kBlack);
      }
    }
    frame(70);
  }

  // f5: black blocks rise
  S->fillSprite(kWhite);
  for (int col = 0; col < kW; col += 8) {
    const uint32_t hh = h32(col, 5);
    const int hgt = 40 + hh % 120;
    if ((hh >> 9) % 3) S->fillRect(col, kH - hgt, 8 - (hh >> 12) % 3, hgt, kBlack);
  }
  S->fillRect(0, kH - 40, kW, 40, kBlack);
  frame(70);

  // f6-f17: the skull swings, gets a floor, then breaks up
  const struct { int8_t a; uint8_t drop, scat, reps; } poses[8] = {
    {-18, 0, 0, 1}, {-10, 0, 0, 2}, {-2, 0, 0, 2}, {6, 0, 0, 2},
    {10, 0, 0, 3}, {12, 30, 6, 1}, {12, 60, 14, 1}, {12, 85, 26, 1} };
  for (int pi = 0; pi < 8; pi++) {
    for (int r = 0; r < poses[pi].reps; r++) {
      S->fillSprite(kBlack);
      cells(boot_skull_shaded, BOOT_SKULL_SHADED_W, BOOT_SKULL_SHADED_H, 120 + pi * 2, 150, 3.1f,
            poses[pi].a, kWhite, poses[pi].drop, poses[pi].scat, 11);
      const uint32_t hh = h32(pi, 9);
      if (pi < 5) {                       // two diagonal speed lines per pose
        for (int L = 0; L < 2; L++) {
          const int x0 = (hh >> (L * 8)) % 120;
          S->fillTriangle(x0, 0, x0 + 3, 0, x0 + 120 + L * 40, kH, kWhite);
        }
      }
      if (pi >= 4 && poses[pi].drop < 60) S->fillRect(18, 262, 204, 4, kWhite);   // floor
      frame(70);
    }
  }

  // f18: shatter, first light beam
  S->fillSprite(kBlack);
  cells(boot_skull_shaded, BOOT_SKULL_SHADED_W, BOOT_SKULL_SHADED_H, 134, 160, 3.1f, 14, kWhite, 92, 40, 11);
  S->fillTriangle(0, 0, 150, 0, 0, 190, kWhite);
  frame(60);

  // f19-f25: the shark slams in on its eye and zooms out
  const struct { float sc; int8_t a; float t; } zoom[7] = {
    {9.0f, 0, 0.00f}, {8.0f, 0, 0.12f}, {6.5f, 0, 0.28f}, {5.2f, 0, 0.45f},
    {4.1f, 0, 0.62f}, {2.5f, 3, 0.82f}, {1.7f, -2, 0.95f} };
  const float eyeX = 104, eyeY = 64, cenX = 75, cenY = 70;
  for (int zi = 0; zi < 7; zi++) {
    S->fillSprite(kBlack);
    logo(zoom[zi].sc, zoom[zi].a, 120, 140,
         eyeX + (cenX - eyeX) * zoom[zi].t, eyeY + (cenY - eyeY) * zoom[zi].t, BRAND_ACCENT);
    if (zi == 0) S->fillTriangle(0, 30, 110, 0, 0, 220, kWhite);
    if (zi == 1) S->fillTriangle(0, kH, 0, 150, 150, kH, kWhite);
    frame(60);
  }

  // f26-f29: lands, stepped line behind, a beam and glitch blocks
  const struct { float sc; int8_t a; } land[4] = { {1.09f, -3}, {1.0f, 0}, {0.98f, 0}, {0.97f, 0} };
  for (int k = 0; k < 4; k++) {
    S->fillSprite(kBlack);
    if (k < 3) stepline(kWhite);
    logo(land[k].sc, land[k].a, 120, 140, cenX, cenY, BRAND_ACCENT);
    if (k == 2) S->fillTriangle(0, 0, 90, 0, 0, 120, kWhite);
    if (k >= 2) glitchBlocks(k, kWhite);
    frame(80);
  }

  // f30-f32: dim dot-matrix outro, the ASCII skull showing through
  const int levels[3] = { 3, 5, 8 };
  const uint16_t ghost = swap16(kGhost);
  for (int k = 0; k < 3; k++) {
    S->fillSprite(kBlack);
    logo(0.96f, 0, 120, 140, cenX, cenY, BRAND_ACCENT);
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

// ═══════════════════════════════════════════ Skull Glitch boot ═══
// The user's Watch Dogs 2-style reel (docs/skull-glitch/), adapted for the
// device: pale-green room wipe, tumbling hourglass, rising blocks, the
// chunky skull (8 poses) swinging and shattering with light beams, the
// SHARKDIV wordmark slamming in at 7x and stepping down, stepped line,
// glitch blocks, then a dot-matrix fade. Phosphor green palette; the
// M5SHARK logo reveal follows in playSharkBoot.

namespace GlitchBoot {

#include "GlitchAssets.h"

// Deterministic noise (same job the ctOS art's hash did).
static uint16_t ghHash(uint16_t a, uint16_t b, uint16_t salt) {
  uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)salt * 83492791u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return (uint16_t)h;
}
#define wdHash ghHash

static const uint8_t* const kPoses[8] = {
  glitch_skull_p0, glitch_skull_p1, glitch_skull_p2, glitch_skull_p3,
  glitch_skull_p4, glitch_skull_p5, glitch_skull_p6, glitch_skull_p7,
};
static const uint8_t* const kHour[3] = { glitch_hour_0, glitch_hour_1, glitch_hour_2 };

static constexpr uint16_t kPaper = 0x9FF3;   // pale green room
static constexpr uint16_t kInk = 0x00E0;     // dark green ink on paper
static constexpr uint16_t kGreen = 0x07E0;   // phosphor green on black
static constexpr uint16_t kDim = 0x03E0;

static void slabs(uint16_t col, int fromY, int toY) {
  // Ragged-edge horizontal slabs, like the reel's pixel wipe edges.
  for (int y = fromY; y < toY; y += 8) {
    const int jitter = (wdHash(y, 7, 0xA5A5) % 5) - 2;
    tft.fillRect(jitter, y, tft.width() - jitter, 8, col);
  }
}

static void drawPose(int p, int16_t x, int16_t y, uint16_t col) {
  tft.drawBitmap(x, y, kPoses[p], 160, 90, col);
}

static void beam(uint16_t col) {
  for (int i = 0; i < 3; i++) {
    tft.fillTriangle(0, 20 + i * 40, 90 + i * 30, 60 + i * 20,
                     0, 110 + i * 30, col);
  }
}

static void glitchBlocks(int n, uint16_t seed) {
  for (int i = 0; i < n; i++) {
    const uint16_t h = wdHash(i, seed, 0x3C3C);
    const int16_t x = h % (tft.width() - 40);
    const int16_t y = (h >> 6) % (tft.height() - 24);
    tft.fillRect(x, y, 18 + (h % 46), 6 + (h % 10),
                 (i & 1) ? kGreen : kPaper);
  }
}

void run() {
  const int16_t W = tft.width();
  const int16_t H = tft.height();
  const int16_t sx = (W - 160) / 2;
  const int16_t sy = 110;

  // f0: pale-green wipe with ragged slabs.
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < 4; i++) { slabs(kPaper, 0, H); delay(30); }
  delay(90);

  // f1-f5: hourglass tumbles across the room, debris flickers.
  for (int p = 0; p < 3; p++) {
    tft.fillRect(0, 0, W, H, kPaper);
    const int hx = (W - 45) / 2 + (p - 1) * 26;
    const int hy = 120 + (p - 1) * 18;
    tft.drawBitmap(hx, hy, kHour[p], 45, 45, kInk);
    for (int i = 0; i < 6; i++) {
      const uint16_t n = wdHash(i, p + 11, 0x5A5A);
      tft.fillRect(n % W, 60 + (n >> 6) % 180, 4, 4, kInk);
    }
    delay(170);
  }

  // f5-f6: black blocks rise from the bottom, swallow the room.
  for (int row = 0; row < 14; row++) {
    const int y = H - (row + 1) * 22;
    const uint16_t n = wdHash(row, 99, 0xC3C3);
    tft.fillRect(-(n % 10), y, W + 20, 22, TFT_BLACK);
    delay(42);
  }
  tft.fillScreen(TFT_BLACK);
  delay(60);

  // f6-f18: the skull swings through its poses.
  static const int8_t swing[6] = { -18, 10, -6, 14, -2, 0 };
  for (int p = 0; p < 5; p++) {
    tft.fillRect(0, 60, W, 190, TFT_BLACK);
    drawPose(p, sx + swing[p], sy, kGreen);
    delay(150);
  }

  // f18: shatter — fragments, light beam, glitch blocks.
  tft.fillRect(0, 60, W, 190, TFT_BLACK);
  drawPose(4, sx, sy, kGreen);
  beam(kPaper);
  delay(60);
  for (int p = 5; p < 8; p++) {
    tft.fillRect(0, 60, W, 190, TFT_BLACK);
    drawPose(p, sx + (p - 6) * 10, sy + (p - 5) * 8, kGreen);
    beam(kDim);
    glitchBlocks(5, p * 31);
    delay(95);
  }

  // f19-f26: SHARKDIV slams in huge and steps down to place.
  static const uint8_t sizes[8] = { 7, 6, 5, 4, 3, 2, 2, 2 };
  for (int i = 0; i < 8; i++) {
    tft.fillRect(0, 60, W, 200, TFT_BLACK);
    // stepped diagonal line behind the wordmark (reel's stepline)
    for (int k = 0; k < 26; k++) {
      tft.fillRect(30 + k * 7, 90 + k * 6, 4, 8, kDim);
    }
    const int8_t jx = (i < 6) ? (wdHash(i, 5, 0x77) % 10) - 5 : 0;
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kGreen, TFT_BLACK);
    tft.drawString("SHARKDIV", W / 2 + jx, 160, sizes[i]);
    tft.setTextDatum(TL_DATUM);
    delay(i < 6 ? 65 : 110);
  }

  // f28: glitch beat over the settled wordmark.
  glitchBlocks(7, 777);
  beam(kGreen);
  delay(90);
  tft.fillRect(0, 60, W, 200, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(kGreen, TFT_BLACK);
  tft.drawString("SHARKDIV", W / 2, 160, 2);
  tft.setTextDatum(TL_DATUM);
  delay(140);

  // f30-f32: dot-matrix fade — everything drops into dim dots.
  for (int pass = 0; pass < 3; pass++) {
    for (int y = 40; y < H - 40; y += 6) {
      for (int x = 6; x < W - 6; x += 6) {
        if ((wdHash(x, y, 0xD0D0) & 3) >= pass) {
          tft.fillRect(x, y, 2, 2, kDim);
        }
      }
    }
    delay(70);
  }
  delay(180);
  tft.fillScreen(TFT_BLACK);
}

}  // namespace GlitchBoot

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
