// SharkGamesDiv.cpp — Snake + Reaction, ported from M5SHARK SharkGames.
// Loop-contract versions (no blocking waits inside a single pass longer
// than a few ms): timers drive the ticks, touches are polled every pass.

#include "SharkGamesDiv.h"
#include "config.h"
#include "utils.h"
#include "Touchscreen.h"

namespace {

// Shared chrome: title bar + nav bar. Games use the nav bar itself as the
// D-pad (LEFT/DOWN/UP/RIGHT slots), center = Exit.
void gameChrome(const char* title, const char* left, const char* down,
                const char* up, const char* right) {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels(left, down, "Exit", up, right);
  if (featureHasTouchNavBar()) {
    featureClearContent(TFT_BLACK);
  } else {
    tft.fillScreen(TFT_BLACK);
  }
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString(String("// ") + title, 6, 28, 2);
  tft.drawFastHLine(0, 38, tft.width(), UI_LINE);
  tft.setTextDatum(TL_DATUM);
  setupTouchscreen();
  redrawTouchButtonBar();
}

bool gameExitRequested() {
  maintainTouchNavBar();
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return true;
  }
  return false;
}

}  // namespace

// ──────────────────────────────────────────────────────────────── Snake ──
namespace SnakeGame {

static constexpr int16_t GRID = 20;
static constexpr int16_t COLS = 12;  // 240 / 20
static constexpr int16_t ROWS = 11;  // body y 44..264
static constexpr int16_t TOP  = 44;
static constexpr uint32_t TICK_MS = 140;

struct Vec2 { int8_t x, y; };
static Vec2  s_snake[132];
static int    s_len = 3;
static int8_t s_dir = 3;      // 0=up 1=down 2=left 3=right
static int8_t s_nextDir = 3;
static Vec2   s_food = {10, 12};
static uint32_t s_score = 0, s_best = 0;
static bool   s_dead = false;
static uint32_t s_lastTick = 0;
static bool   s_drewDeath = false;

static void cell(int8_t cx, int8_t cy, uint16_t col) {
  tft.fillRect(cx * GRID + 1, TOP + cy * GRID + 1, GRID - 2, GRID - 2, col);
}

static void spawnFood() {
  bool ok = false;
  while (!ok) {
    s_food.x = random(0, COLS);
    s_food.y = random(0, ROWS);
    ok = true;
    for (int i = 0; i < s_len; i++) {
      if (s_snake[i].x == s_food.x && s_snake[i].y == s_food.y) { ok = false; break; }
    }
  }
}

static void reset() {
  s_len = 3; s_dir = 3; s_nextDir = 3; s_score = 0;
  s_dead = false; s_drewDeath = false;
  for (int i = 0; i < s_len; i++) { s_snake[i].x = 5 - i; s_snake[i].y = 8; }
  spawnFood();
  tft.fillRect(0, TOP, tft.width(), ROWS * GRID, TFT_BLACK);
  cell(s_food.x, s_food.y, UI_WARN);
  cell(s_snake[0].x, s_snake[0].y, UI_ICON);
  for (int i = 1; i < s_len; i++) cell(s_snake[i].x, s_snake[i].y, UI_TEXT);
}

static void hud() {
  tft.setTextDatum(TL_DATUM);
  tft.fillRect(0, 266, tft.width(), 16, TFT_BLACK);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("Score: " + String(s_score), 6, 268, 2);
  tft.setTextDatum(MR_DATUM);
  tft.drawString("Len: " + String(s_len), tft.width() - 6, 268, 2);
  tft.setTextDatum(TL_DATUM);
}

void snakeSetup() {
  gameChrome("SNAKE", "◄", "▼", "▲", "►");
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("nav pad steers - eat the amber cells", tft.width() / 2, 160, 1);
  tft.setTextDatum(TL_DATUM);
  s_best = 0;
  s_lastTick = millis();
  reset();
  hud();
}

void snakeLoop() {
  if (gameExitRequested()) return;

  int8_t nd = -1;
  if (isTouchNavButtonPressedEdge(BTN_UP) || isButtonPressed(BTN_UP)) nd = 0;
  else if (isTouchNavButtonPressedEdge(BTN_DOWN) || isButtonPressed(BTN_DOWN)) nd = 1;
  else if (isTouchNavButtonPressedEdge(BTN_LEFT) || isButtonPressed(BTN_LEFT)) nd = 2;
  else if (isTouchNavButtonPressedEdge(BTN_RIGHT) || isButtonPressed(BTN_RIGHT)) nd = 3;
  if (nd >= 0) {
    // No 180-degree reversals.
    if (!(nd == 0 && s_dir == 1) && !(nd == 1 && s_dir == 0) &&
        !(nd == 2 && s_dir == 3) && !(nd == 3 && s_dir == 2)) {
      s_nextDir = nd;
    }
  }

  if (s_dead) {
    if (!s_drewDeath) {
      if (s_score > s_best) s_best = s_score;
      tft.fillRect(20, 120, 200, 80, TFT_BLACK);
      tft.drawRect(20, 120, 200, 80, UI_WARN);
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(UI_WARN, TFT_BLACK);
      tft.drawString("GAME OVER", tft.width() / 2, 140, 4);
      tft.setTextColor(UI_TEXT, TFT_BLACK);
      tft.drawString("Score " + String(s_score) + "  Best " + String(s_best),
                     tft.width() / 2, 176, 2);
      tft.setTextDatum(TL_DATUM);
      s_drewDeath = true;
    }
    // Any nav direction or screen tap restarts.
    int tx, ty;
    if (nd >= 0 || readTouchXYDismiss(tx, ty)) {
      reset();
      hud();
    }
    delay(20);
    return;
  }

  if (millis() - s_lastTick < TICK_MS) {
    delay(5);
    return;
  }
  s_lastTick = millis();
  s_dir = s_nextDir;

  Vec2 head = s_snake[0];
  if (s_dir == 0) head.y--;
  else if (s_dir == 1) head.y++;
  else if (s_dir == 2) head.x--;
  else head.x++;

  if (head.x < 0 || head.x >= COLS || head.y < 0 || head.y >= ROWS) {
    s_dead = true;
    return;
  }
  for (int i = 0; i < s_len; i++) {
    if (s_snake[i].x == head.x && s_snake[i].y == head.y) { s_dead = true; return; }
  }

  const bool ate = (head.x == s_food.x && head.y == s_food.y);
  if (ate) {
    s_score++;
    spawnFood();
  } else {
    cell(s_snake[s_len - 1].x, s_snake[s_len - 1].y, TFT_BLACK);
  }
  for (int i = s_len - 1; i > 0; i--) s_snake[i] = s_snake[i - 1];
  s_snake[0] = head;
  if (ate && s_len < 132) s_len++;

  cell(s_food.x, s_food.y, UI_WARN);
  cell(s_snake[0].x, s_snake[0].y, UI_ICON);
  for (int i = 1; i < s_len; i++) cell(s_snake[i].x, s_snake[i].y, UI_TEXT);
  hud();
}

void exit() {}

}  // namespace SnakeGame

// ───────────────────────────────────────────────────────────── Reaction ──
namespace ReactionGame {

enum Phase : uint8_t { R_IDLE, R_ARMED, R_GREEN, R_RESULT, R_DONE };
static constexpr uint8_t ROUNDS = 5;

static Phase   s_phase = R_IDLE;
static uint8_t s_round = 0;
static uint32_t s_times[ROUNDS];
static uint32_t s_best = 999999;
static uint32_t s_armAt = 0;
static uint32_t s_greenAt = 0;
static bool     s_fouled = false;

static void drawIdle() {
  tft.fillRect(0, 44, tft.width(), 220, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_ICON, TFT_BLACK);
  tft.drawString("REACTION TEST", tft.width() / 2, 110, 3);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("5 rounds - tap when the panel turns green", tft.width() / 2, 150, 2);
  tft.drawString("tap anywhere to start", tft.width() / 2, 190, 1);
  tft.setTextDatum(TL_DATUM);
}

static void drawArmed() {
  tft.fillRect(0, 60, tft.width(), 160, TFT_BLACK);
  tft.drawRect(10, 60, tft.width() - 20, 160, UI_LINE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.drawString("Round " + String(s_round + 1) + "/" + String(ROUNDS), tft.width() / 2, 110, 2);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("wait for green...", tft.width() / 2, 150, 2);
  tft.setTextDatum(TL_DATUM);
}

static void drawGreen() {
  tft.fillRect(0, 60, tft.width(), 160, UI_ICON);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, UI_ICON);
  tft.drawString("TAP!", tft.width() / 2, 130, 4);
  tft.setTextDatum(TL_DATUM);
}

void reactionSetup() {
  gameChrome("REACTION", nullptr, nullptr, nullptr, nullptr);
  s_phase = R_IDLE;
  s_round = 0;
  s_best = 999999;
  for (uint8_t i = 0; i < ROUNDS; i++) s_times[i] = 0;
  drawIdle();
}

void reactionLoop() {
  if (gameExitRequested()) return;

  // Nav bar taps are direction/exit slots; the game reads raw screen taps
  // above the nav bar only.
  int tx, ty;
  const bool tapped = readTouchXY(tx, ty) && ty < touchNavContentBottomY();

  switch (s_phase) {
    case R_IDLE:
      if (tapped) {
        s_round = 0;
        s_phase = R_ARMED;
        s_armAt = millis() + random(1000, 4000);
        drawArmed();
      }
      break;

    case R_ARMED:
      if (tapped) {
        // Jumped the gun — show it and re-arm the same round.
        tft.fillRect(0, 60, tft.width(), 160, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(UI_WARN, TFT_BLACK);
        tft.drawString("TOO EARLY", tft.width() / 2, 130, 3);
        tft.setTextDatum(TL_DATUM);
        delay(900);
        s_armAt = millis() + random(1000, 4000);
        drawArmed();
      } else if ((int32_t)(millis() - s_armAt) >= 0) {
        s_phase = R_GREEN;
        s_greenAt = micros();
        drawGreen();
      }
      break;

    case R_GREEN:
      if (tapped) {
        const uint32_t ms = (micros() - s_greenAt) / 1000;
        s_times[s_round] = ms;
        if (ms < s_best) s_best = ms;
        s_phase = R_RESULT;
        tft.fillRect(0, 60, tft.width(), 160, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(ms < 250 ? UI_ICON : UI_TEXT, TFT_BLACK);
        tft.drawString(String(ms) + " ms", tft.width() / 2, 130, 4);
        tft.setTextDatum(TL_DATUM);
        s_armAt = millis() + 1200;  // reuse as the result dwell
      }
      break;

    case R_RESULT:
      if ((int32_t)(millis() - s_armAt) >= 0) {
        s_round++;
        if (s_round >= ROUNDS) {
          s_phase = R_DONE;
          tft.fillRect(0, 44, tft.width(), 220, TFT_BLACK);
          tft.setTextDatum(MC_DATUM);
          tft.setTextColor(UI_ICON, TFT_BLACK);
          tft.drawString("RESULTS", tft.width() / 2, 70, 3);
          tft.setTextColor(UI_TEXT, TFT_BLACK);
          for (uint8_t i = 0; i < ROUNDS; i++) {
            tft.drawString("R" + String(i + 1) + ": " + String(s_times[i]) + " ms",
                           tft.width() / 2, 110 + i * 22, 2);
          }
          tft.setTextColor(UI_WARN, TFT_BLACK);
          tft.drawString("Best: " + String(s_best) + " ms", tft.width() / 2, 236, 3);
          tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
          tft.drawString("tap to play again", tft.width() / 2, 258, 1);
          tft.setTextDatum(TL_DATUM);
        } else {
          s_phase = R_ARMED;
          s_armAt = millis() + random(1000, 4000);
          drawArmed();
        }
      }
      break;

    case R_DONE:
      if (tapped) {
        s_round = 0;
        s_best = 999999;
        for (uint8_t i = 0; i < ROUNDS; i++) s_times[i] = 0;
        s_phase = R_ARMED;
        s_armAt = millis() + random(1000, 4000);
        drawArmed();
      }
      break;
  }
  delay(15);
}

void exit() {}

}  // namespace ReactionGame
