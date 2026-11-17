// Slot machine screen (S2).
//
// Requirement: a slot machine played with the physical button, the on-board RGB
// LED reacting on a win, casino colours, and it has to look good.
//
// The palette is the point: dark green felt, gold reel frames, crimson payline
// arrows. A win runs the jackpot strobe on the LED bead through rgbCelebrate()
// and flashes the reel frame on screen, so the reaction is visible from both
// sides of the board.
#include "rgb.h"
#include "screens.h"
#include "theme.h"
#include "settings.h"

// --- geometry ---------------------------------------------------------------

#define REEL_W 84
#define REEL_H 96
#define REEL_GAP 8
#define REEL_X0 ((DISPLAY_W - (REEL_W * 3 + REEL_GAP * 2)) / 2)
// No header band above the reels: they start 14 px from the top so the drum
// frames, the stats line and the message keep their spacing.
#define REEL_Y 14
#define REEL_MID (REEL_Y + REEL_H / 2)

#define MSG_Y 132
#define STATS_Y 118

// --- palette ----------------------------------------------------------------

#define GOLD uiAccent()
#define AMBER uiAccentDim()
#define CRIMSON gfxHex(0xDC143C)
#define SYM_RED gfxHex(0xE8112D)
#define SYM_CYAN gfxHex(0x40E0D0)
#define SYM_YELLOW gfxHex(0xFFE44D)
#define SYM_GREEN gfxHex(0x2ECC40)
#define FELT_TOP 0x0B3D1E
#define FELT_BOTTOM 0x021006

// --- symbols ----------------------------------------------------------------

enum {
  SYM_SEVEN = 0,
  SYM_BAR,
  SYM_CHERRY,
  SYM_BELL,
  SYM_DIAMOND,
  SYM_STAR,
  SYM_LEMON,
  SYM_COUNT
};

// Three of a kind pays these; the rarer the symbol, the deeper the payout.
static const uint16_t PAYOUT3[SYM_COUNT] = {500, 200, 80, 150, 120, 100, 60};
#define PAYOUT2 20

// --- state ------------------------------------------------------------------

enum SlotState { SLOT_IDLE, SLOT_SPIN, SLOT_RESULT };

static SlotState s_state = SLOT_IDLE;
static uint32_t s_credits = 100;
static uint8_t s_display[3] = {SYM_SEVEN, SYM_SEVEN, SYM_SEVEN};
static uint8_t s_final[3] = {SYM_SEVEN, SYM_SEVEN, SYM_SEVEN};
static uint32_t s_stopAt[3] = {0, 0, 0};
static uint32_t s_stateUntil = 0;
static uint16_t s_lastWin = 0;
static char s_message[28] = "";
static uint32_t s_messageUntil = 0;
static uint32_t s_lastDraw = 0;
static bool s_needRedraw = true;
static uint32_t s_rng = 0x2545F491;

static uint32_t nextRandom() {
  // xorshift32: the game only needs cheap, non-cryptographic variety.
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static void setMessage(const char *text, uint32_t nowMs, uint32_t durationMs) {
  snprintf(s_message, sizeof(s_message), "%s", text);
  s_messageUntil = nowMs + durationMs;
}

// --- drawing ----------------------------------------------------------------

static void drawFelt() {
  // A vertical gradient is enough to read as felt under a lamp, and it costs
  // one filled row per scanline.
  for (int y = 0; y < DISPLAY_H; y++) {
    int t = y * 255 / (DISPLAY_H - 1);
    uint8_t r = (uint8_t)(((FELT_TOP >> 16) & 0xFF) * (255 - t) / 255 +
                          ((FELT_BOTTOM >> 16) & 0xFF) * t / 255);
    uint8_t g = (uint8_t)(((FELT_TOP >> 8) & 0xFF) * (255 - t) / 255 +
                          ((FELT_BOTTOM >> 8) & 0xFF) * t / 255);
    uint8_t b = (uint8_t)((FELT_TOP & 0xFF) * (255 - t) / 255 +
                          (FELT_BOTTOM & 0xFF) * t / 255);
    gfxHLine(0, y, DISPLAY_W, gfxColor(r, g, b));
  }
}

static void textAtCenter(int cx, int cy, int scale, const char *text,
                         uint16_t color) {
  int w = gfxTextWidth(scale, text);
  gfxText(cx - w / 2, cy - (7 * scale) / 2, scale, text, color);
}

static void drawSeven(int cx, int cy) {
  textAtCenter(cx, cy, 5, "7", SYM_RED);
}

static void drawBar(int cx, int cy) {
  gfxFillRect(cx - 22, cy - 11, 44, 22, GFX_BLACK);
  gfxRect(cx - 22, cy - 11, 44, 22, GFX_WHITE);
  gfxRect(cx - 21, cy - 10, 42, 20, GFX_WHITE);
  textAtCenter(cx, cy, 2, "BAR", GFX_WHITE);
}

static void drawCherry(int cx, int cy) {
  // Stems first so the fruit overlaps them.
  gfxLine(cx - 8, cy + 6, cx + 2, cy - 16, SYM_GREEN);
  gfxLine(cx + 9, cy + 8, cx + 2, cy - 16, SYM_GREEN);
  gfxFillEllipse(cx + 12, cy - 15, 7, 3, SYM_GREEN);
  gfxFillCircle(cx - 9, cy + 6, 9, SYM_RED);
  gfxFillCircle(cx + 9, cy + 8, 9, SYM_RED);
  // A single specular dot each keeps them from reading as flat discs.
  gfxFillCircle(cx - 12, cy + 3, 2, gfxHex(0xFF8A8A));
  gfxFillCircle(cx + 6, cy + 5, 2, gfxHex(0xFF8A8A));
}

static void drawBell(int cx, int cy) {
  const int xs[7] = {cx - 16, cx - 10, cx - 6, cx, cx + 6, cx + 10, cx + 16};
  const int ys[7] = {cy + 8, cy - 4, cy - 14, cy - 16, cy - 14, cy - 4, cy + 8};
  gfxFillPolygon(xs, ys, 7, GOLD);
  gfxFillRect(cx - 17, cy + 8, 34, 5, AMBER);
  gfxFillCircle(cx, cy + 16, 4, AMBER);
  gfxFillRect(cx - 4, cy - 12, 3, 18, gfxHex(0xFFF0A0));
}

static void drawDiamond(int cx, int cy) {
  const int xs[4] = {cx, cx + 14, cx, cx - 14};
  const int ys[4] = {cy - 18, cy, cy + 18, cy};
  gfxFillPolygon(xs, ys, 4, SYM_CYAN);
  const int ix[4] = {cx, cx + 7, cx, cx - 7};
  const int iy[4] = {cy - 9, cy, cy + 9, cy};
  gfxFillPolygon(ix, iy, 4, GFX_WHITE);
}

static void drawStar(int cx, int cy) {
  int xs[10], ys[10];
  for (int i = 0; i < 10; i++) {
    // Alternate outer and inner radius; start at the top point.
    float r = (i % 2 == 0) ? 18.0f : 7.5f;
    float a = -1.5707963f + (float)i * 0.62831853f;  // 36 degrees per step
    xs[i] = cx + (int)(r * cosf(a));
    ys[i] = cy + (int)(r * sinf(a));
  }
  gfxFillPolygon(xs, ys, 10, SYM_YELLOW);
}

static void drawLemon(int cx, int cy) {
  gfxFillEllipse(cx, cy + 2, 14, 10, SYM_YELLOW);
  gfxFillEllipse(cx - 4, cy, 5, 3, gfxHex(0xFFF6B0));
  gfxFillEllipse(cx + 11, cy - 9, 6, 3, SYM_GREEN);
}

static void drawSymbol(uint8_t symbol, int cx, int cy) {
  switch (symbol) {
    case SYM_SEVEN: drawSeven(cx, cy); break;
    case SYM_BAR: drawBar(cx, cy); break;
    case SYM_CHERRY: drawCherry(cx, cy); break;
    case SYM_BELL: drawBell(cx, cy); break;
    case SYM_DIAMOND: drawDiamond(cx, cy); break;
    case SYM_STAR: drawStar(cx, cy); break;
    case SYM_LEMON: drawLemon(cx, cy); break;
    default: break;
  }
}

static void drawReel(int index, uint8_t symbol, bool spinning, bool winFlash,
                     uint32_t nowMs) {
  int x = REEL_X0 + index * (REEL_W + REEL_GAP);

  uint16_t frame = (winFlash && ((nowMs / 180) % 2)) ? GFX_WHITE : GOLD;
  gfxFillRect(x - 3, REEL_Y - 3, REEL_W + 6, REEL_H + 6, frame);
  gfxFillRect(x, REEL_Y, REEL_W, REEL_H, GFX_BLACK);

  // Curvature hint: darken the outer columns of the drum.
  gfxFillRect(x, REEL_Y, 7, REEL_H, gfxHex(0x0C0C0C));
  gfxFillRect(x + REEL_W - 7, REEL_Y, 7, REEL_H, gfxHex(0x0C0C0C));

  // While a drum is still turning the symbol jitters one pixel up and down so
  // the reel reads as moving even though only one symbol is drawn.
  int jitter = spinning ? (int)((nowMs / 45 + index) % 3) - 1 : 0;
  drawSymbol(symbol, x + REEL_W / 2, REEL_Y + REEL_H / 2 + jitter);
}

static void drawPaylineArrows() {
  int leftX = REEL_X0 - 3;
  int rightX = REEL_X0 + REEL_W * 3 + REEL_GAP * 2 + 2;
  for (int i = 0; i < 4; i++) {
    gfxHLine(leftX - 8 + i, REEL_MID - 3 + i, 8 - i, CRIMSON);
    gfxHLine(leftX - 8 + i, REEL_MID + 3 - i, 8 - i, CRIMSON);
    gfxHLine(rightX + i, REEL_MID - 3 + i, 8 - i, CRIMSON);
    gfxHLine(rightX + i, REEL_MID + 3 - i, 8 - i, CRIMSON);
  }
}

static void drawStats() {
  char left[28];
  snprintf(left, sizeof(left), "CREDITOS %lu", (unsigned long)s_credits);
  gfxText(10, STATS_Y, 1, left, GOLD);
  char right[28];
  snprintf(right, sizeof(right), "RECORD %u",
           (unsigned)settingsGet().slotHighScore);
  gfxTextRight(DISPLAY_W - 10, STATS_Y, 1, right, GOLD);
}

static void drawMessage(uint32_t nowMs) {
  if (!s_message[0] || nowMs >= s_messageUntil) return;

  bool isWin = s_state == SLOT_RESULT && s_lastWin > 0;
  if (isWin) {
    // Win text pulses; a static "WIN" reads as a label, a pulsing one as news.
    if (((nowMs / 220) % 2) == 0) return;
    uint16_t color = (s_lastWin >= 500) ? GFX_WHITE : GOLD;
    textAtCenter(DISPLAY_W / 2, MSG_Y + 4, 2, s_message, color);
  } else {
    int w = gfxTextWidth(1, s_message);
    gfxText((DISPLAY_W - w) / 2, MSG_Y, 1, s_message, GFX_WHITE);
  }
}

static void draw(uint32_t nowMs) {
  drawFelt();

  drawPaylineArrows();

  bool winFlash = (s_state == SLOT_RESULT) && (s_lastWin > 0);
  for (int i = 0; i < 3; i++) {
    drawReel(i, s_display[i], s_state == SLOT_SPIN && nowMs < s_stopAt[i],
             winFlash, nowMs);
  }

  drawStats();
  drawMessage(nowMs);
  chromeFooter(s_state == SLOT_SPIN ? "SUERTE...   MANTENE 3S"
                                    : "TOCA PARA GIRAR   MANTENE 3S");
}

// --- payouts ----------------------------------------------------------------

static uint16_t evaluate(uint8_t a, uint8_t b, uint8_t c) {
  if (a == b && b == c) return PAYOUT3[a];
  if (a == b || b == c || a == c) return PAYOUT2;
  return 0;
}

static void finishSpin(uint32_t nowMs) {
  uint16_t win = evaluate(s_final[0], s_final[1], s_final[2]);
  s_lastWin = win;

  if (win > 0) {
    s_credits += win;
    if (win > settingsGet().slotHighScore) {
      settingsMutable().slotHighScore = win;
      settingsSave();
    }
    // The LED celebration scales with the win, and three sevens get the full
    // jackpot strobe.
    bool jackpot = (s_final[0] == SYM_SEVEN && s_final[1] == SYM_SEVEN &&
                    s_final[2] == SYM_SEVEN);
    rgbCelebrate(jackpot ? 4000 : (uint32_t)(900 + win * 4));
    if (jackpot) {
      setMessage("JACKPOT!", nowMs, 2600);
    } else {
      char m[28];
      snprintf(m, sizeof(m), "GANASTE +%u", (unsigned)win);
      setMessage(m, nowMs, 2600);
    }
  } else {
    rgbPulse(120, 8, 8, 450);
    setMessage("SIN SUERTE", nowMs, 2000);
  }

  s_state = SLOT_RESULT;
  s_stateUntil = nowMs + 2600;
}

// --- screen interface -------------------------------------------------------

void slotEnter() {
  s_state = SLOT_IDLE;
  s_message[0] = '\0';
  s_lastWin = 0;
  s_display[0] = s_display[1] = s_display[2] = SYM_SEVEN;
  s_needRedraw = true;
}

void slotShortPress() {
  uint32_t now = millis();
  if (s_state == SLOT_SPIN) return;

  if (s_credits < 10) {
    // The machine never dead-ends: a player with no credits gets a top-up
    // instead of a screen that no longer responds.
    s_credits += 50;
    setMessage("CREDITOS +50", now, 1500);
    s_state = SLOT_IDLE;
    s_needRedraw = true;
    return;
  }

  s_credits -= 10;
  for (int i = 0; i < 3; i++) {
    s_final[i] = (uint8_t)(nextRandom() % SYM_COUNT);
    s_stopAt[i] = now + 700 + (uint32_t)i * 450;
  }
  s_state = SLOT_SPIN;
  s_lastWin = 0;
  s_message[0] = '\0';
  rgbPulse(255, 215, 0, 250);
  s_needRedraw = true;
}

bool slotTick(uint32_t nowMs) {
  bool changed = false;

  if (s_state == SLOT_SPIN) {
    for (int i = 0; i < 3; i++) {
      if (nowMs >= s_stopAt[i]) {
        s_display[i] = s_final[i];
      } else {
        s_display[i] = (uint8_t)(nextRandom() % SYM_COUNT);
      }
    }
    if (nowMs >= s_stopAt[2]) finishSpin(nowMs);
    changed = true;
  } else if (s_state == SLOT_RESULT && nowMs >= s_stateUntil) {
    s_state = SLOT_IDLE;
    changed = true;
  }

  // Animated states repaint on a fixed cadence; idle repaints only on demand.
  bool animating = (s_state != SLOT_IDLE) ||
                   (s_message[0] && nowMs < s_messageUntil);
  if (animating && (uint32_t)(nowMs - s_lastDraw) >= 60) changed = true;
  if (s_needRedraw) changed = true;

  if (!changed) return false;

  s_lastDraw = nowMs;
  s_needRedraw = false;
  draw(nowMs);
  return true;
}
