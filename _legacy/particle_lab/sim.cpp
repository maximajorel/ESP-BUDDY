#include <string.h>

#include <esp32-hal-psram.h>

#include "sim.h"

#include <math.h>

#include "Display_ST7789.h"

// ---------------------------------------------------------------- storage ---

static uint8_t *s_mat = nullptr;    // sand: 0 empty, 1 grain
static uint8_t *s_shade = nullptr;  // sand: per-grain colour variant
static uint8_t *s_moved = nullptr;  // sand: grain already moved this frame
static uint8_t *s_water = nullptr;  // water: 0..255 mass in the cell

static int s_mode = SIM_MODE_SAND;

static inline int idx(int x, int y) { return y * SIM_GW + x; }
static inline bool inBounds(int x, int y) {
  return x >= 0 && x < SIM_GW && y >= 0 && y < SIM_GH;
}

// --------------------------------------------------------------- palettes ---

// Palette entries are stored already byte-swapped.
static uint16_t s_bgSw;
static uint16_t s_sandSw[4];
static uint16_t s_waterSw[32];

static uint16_t rgb565Sw(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return LCD_Swap16(c);
}

static void buildPalettes() {
  s_bgSw = rgb565Sw(10, 12, 18);

  // Four warm variants so the pile is not a flat block of one colour.
  s_sandSw[0] = rgb565Sw(226, 178, 96);
  s_sandSw[1] = rgb565Sw(206, 154, 74);
  s_sandSw[2] = rgb565Sw(240, 200, 130);
  s_sandSw[3] = rgb565Sw(184, 134, 62);

  // Water depth ramp: deep navy at the bottom of the ramp, bright cyan on top.
  for (int i = 0; i < 32; i++) {
    float t = i / 31.0f;
    uint8_t r = (uint8_t)(12 + t * 78);
    uint8_t g = (uint8_t)(46 + t * 150);
    uint8_t b = (uint8_t)(104 + t * 130);
    s_waterSw[i] = rgb565Sw(r, g, b);
  }
}

// ------------------------------------------------------------------ setup ---

bool simBegin() {
  s_mat = (uint8_t *)ps_malloc(SIM_GW * SIM_GH);
  s_shade = (uint8_t *)ps_malloc(SIM_GW * SIM_GH);
  s_moved = (uint8_t *)ps_malloc(SIM_GW * SIM_GH);
  s_water = (uint8_t *)ps_malloc(SIM_GW * SIM_GH);
  if (!s_mat || !s_shade || !s_moved || !s_water) return false;

  buildPalettes();
  simReset();
  return true;
}

void simSetMode(int mode) {
  if (mode != SIM_MODE_SAND && mode != SIM_MODE_WATER) return;
  if (mode == s_mode) return;
  s_mode = mode;
  simReset();
}

int simMode() { return s_mode; }

void simReset() {
  if (!s_mat) return;
  memset(s_mat, 0, SIM_GW * SIM_GH);
  memset(s_shade, 0, SIM_GW * SIM_GH);
  memset(s_moved, 0, SIM_GW * SIM_GH);
  memset(s_water, 0, SIM_GW * SIM_GH);

  if (s_mode == SIM_MODE_SAND) {
    // Start with a bed of sand filling the lower half, so the very first
    // tilt produces a visible avalanche instead of an empty screen.
    int fromY = (int)(SIM_GH * 0.45f);
    for (int y = fromY; y < SIM_GH; y++) {
      for (int x = 0; x < SIM_GW; x++) {
        int i = idx(x, y);
        s_mat[i] = 1;
        s_shade[i] = (uint8_t)((x * 7 + y * 13) & 3);
      }
    }
  } else {
    // A tank of water in the lower half. Tilting it makes the surface slosh
    // and then settle flat again, which is the whole point of this view.
    int fromY = (int)(SIM_GH * 0.55f);
    for (int y = fromY; y < SIM_GH; y++) {
      for (int x = 0; x < SIM_GW; x++) s_water[idx(x, y)] = 255;
    }
  }
}

// ------------------------------------------------------------------- sand ---

static int8_t s_ox[8];
static int8_t s_oy[8];

// Orders the eight neighbours by how well they follow gravity. Diagonals are
// scaled by 1/sqrt(2) so a diagonal move must be genuinely "more downward"
// than a straight one to win.
static void buildSandOrder(float gx, float gy) {
  static const int8_t bx[8] = {0, -1, 1, -1, 1, -1, 1, 0};
  static const int8_t by[8] = {1, 1, 1, 0, 0, -1, -1, -1};
  float score[8];
  bool used[8];

  for (int i = 0; i < 8; i++) {
    float w = (bx[i] != 0 && by[i] != 0) ? 0.7071f : 1.0f;
    score[i] = (bx[i] * gx + by[i] * gy) * w;
    used[i] = false;
  }
  for (int k = 0; k < 8; k++) {
    int best = -1;
    for (int i = 0; i < 8; i++) {
      if (used[i]) continue;
      if (best < 0 || score[i] > score[best]) best = i;
    }
    used[best] = true;
    s_ox[k] = bx[best];
    s_oy[k] = by[best];
  }
}

static void simStepSand(float gx, float gy) {
  buildSandOrder(gx, gy);
  memset(s_moved, 0, SIM_GW * SIM_GH);

  // Alternate the horizontal scan direction every frame; a fixed order makes
  // the pile drift sideways over time.
  static bool flip = false;
  flip = !flip;

  for (int row = 0; row < SIM_GH; row++) {
    int y = SIM_GH - 1 - row;
    for (int k = 0; k < SIM_GW; k++) {
      int x = flip ? k : (SIM_GW - 1 - k);
      int i = idx(x, y);
      if (!s_mat[i] || s_moved[i]) continue;

      for (int o = 0; o < 8; o++) {
        int nx = x + s_ox[o];
        int ny = y + s_oy[o];
        if (!inBounds(nx, ny)) continue;
        int j = idx(nx, ny);
        if (s_mat[j]) continue;

        s_mat[j] = s_mat[i];
        s_shade[j] = s_shade[i];
        s_mat[i] = 0;
        s_moved[j] = 1;  // a grain only travels one cell per frame
        break;
      }
    }
  }
}

// ------------------------------------------------------------------ water ---

// Water is constrained to move along one of the four cardinal directions. The
// small dead zone keeps the axis from flickering while the board is nearly
// level, which would make the surface jitter.
static int s_axX = 0;
static int s_axY = 1;

static void updateWaterAxis(float gx, float gy) {
  float ax = fabsf(gx);
  float ay = fabsf(gy);
  if (ax > ay + 0.15f) {
    s_axX = (gx > 0) ? 1 : -1;
    s_axY = 0;
  } else if (ay > ax + 0.15f) {
    s_axY = (gy > 0) ? 1 : -1;
    s_axX = 0;
  }
}

// A "line" is a row or a column, whichever runs perpendicular to gravity. Cells
// inside a line are numbered from the end gravity pushes towards, so k = 0 is
// always the deepest cell of that line.
#define SIM_MAX_LINES (SIM_GH > SIM_GW ? SIM_GH : SIM_GW)

static uint16_t s_lineTotal[SIM_MAX_LINES];
static int32_t s_lineDelta[SIM_MAX_LINES];

static inline void lineCell(int line, int k, bool vertical, int &x, int &y) {
  if (vertical) {
    x = line;
    y = (s_axY > 0) ? (SIM_GH - 1 - k) : k;
  } else {
    y = line;
    x = (s_axX > 0) ? (SIM_GW - 1 - k) : k;
  }
}

static void simStepWater(float gx, float gy) {
  updateWaterAxis(gx, gy);

  const bool vertical = (s_axY != 0);
  const int lines = vertical ? SIM_GW : SIM_GH;
  const int lineLen = vertical ? SIM_GH : SIM_GW;

  // 1. Measure how much water each line holds.
  uint32_t grand = 0;
  for (int j = 0; j < lines; j++) {
    uint32_t total = 0;
    for (int k = 0; k < lineLen; k++) {
      int x, y;
      lineCell(j, k, vertical, x, y);
      total += s_water[idx(x, y)];
    }
    s_lineTotal[j] = (uint16_t)total;
    grand += total;
  }
  if (!grand) return;

  // 2. A liquid at rest has the same amount in every line, which puts the
  //    surface perpendicular to gravity. Move part way there each frame rather
  //    than jumping, so the surface slides instead of teleporting.
  const uint32_t base = grand / lines;
  const uint32_t remainder = grand % lines;
  for (int j = 0; j < lines; j++) {
    int32_t target = (int32_t)base + ((uint32_t)j < remainder ? 1 : 0);
    s_lineDelta[j] = (int32_t)(((int64_t)(target - (int32_t)s_lineTotal[j])) / 2);
  }

  // 3. Hand water from over-full lines to under-full ones. Only transfers
  //    between lines ever happen, so the total volume is conserved exactly and
  //    the tank can never leak.
  int donor = 0;
  int receiver = 0;
  while (true) {
    while (donor < lines && s_lineDelta[donor] >= 0) donor++;
    while (receiver < lines && s_lineDelta[receiver] <= 0) receiver++;
    if (donor >= lines || receiver >= lines) break;

    int32_t amount = (-s_lineDelta[donor] < s_lineDelta[receiver])
                         ? -s_lineDelta[donor]
                         : s_lineDelta[receiver];
    s_lineTotal[donor] -= (uint16_t)amount;
    s_lineTotal[receiver] += (uint16_t)amount;
    s_lineDelta[donor] += amount;
    s_lineDelta[receiver] -= amount;
    if (s_lineDelta[donor] == 0) donor++;
    if (s_lineDelta[receiver] == 0) receiver++;
  }

  // 4. Rewrite the cells: each line packs its water against the gravity end,
  //    so the free surface always sits on top of the filled part.
  for (int j = 0; j < lines; j++) {
    int remaining = s_lineTotal[j];
    for (int k = 0; k < lineLen; k++) {
      int amount = remaining > 255 ? 255 : remaining;
      remaining -= amount;
      int x, y;
      lineCell(j, k, vertical, x, y);
      s_water[idx(x, y)] = (uint8_t)amount;
    }
  }
}

// ------------------------------------------------------------------ frame ---

void simStep(float gx, float gy) {
  if (!s_mat) return;
  if (s_mode == SIM_MODE_SAND) {
    simStepSand(gx, gy);
  } else {
    simStepWater(gx, gy);
  }
}

static inline void putCell(uint16_t *fb, int x, int y, uint16_t colorSw) {
  uint16_t *p = fb + (y * SIM_CELL) * LCD_WIDTH + (x * SIM_CELL);
  p[0] = colorSw;
  p[1] = colorSw;
  p[LCD_WIDTH] = colorSw;
  p[LCD_WIDTH + 1] = colorSw;
}

void simRender(uint16_t *fb) {
  for (int i = 0; i < LCD_PIXELS; i++) fb[i] = s_bgSw;

  if (s_mode == SIM_MODE_SAND) {
    for (int y = 0; y < SIM_GH; y++) {
      for (int x = 0; x < SIM_GW; x++) {
        int i = idx(x, y);
        if (s_mat[i]) putCell(fb, x, y, s_sandSw[s_shade[i] & 3]);
      }
    }
    return;
  }

  for (int y = 0; y < SIM_GH; y++) {
    for (int x = 0; x < SIM_GW; x++) {
      int i = idx(x, y);
      int amount = s_water[i];
      if (!amount) continue;

      // Depth shading, plus a bright rim where the surface meets empty space.
      int shade = amount >> 3;  // 0..31
      bool surface = (s_axY > 0) ? (y == 0 || s_water[idx(x, y - 1)] == 0)
                                 : (y == SIM_GH - 1 || s_water[idx(x, y + 1)] == 0);
      if (s_axX != 0) {
        surface = (s_axX > 0) ? (x == 0 || s_water[idx(x - 1, y)] == 0)
                              : (x == SIM_GW - 1 || s_water[idx(x + 1, y)] == 0);
      }
      if (surface) shade = 31;
      if (shade < 0) shade = 0;
      if (shade > 31) shade = 31;
      putCell(fb, x, y, s_waterSw[shade]);
    }
  }
}

// ------------------------------------------------------------ diagnostics ---

int simParticles() {
  if (!s_mat) return 0;
  int n = 0;
  if (s_mode == SIM_MODE_SAND) {
    for (int i = 0; i < SIM_GW * SIM_GH; i++) n += s_mat[i] ? 1 : 0;
  } else {
    for (int i = 0; i < SIM_GW * SIM_GH; i++) n += s_water[i] ? 1 : 0;
  }
  return n;
}

static void centroid(float &cx, float &cy) {
  double sumX = 0, sumY = 0, weight = 0;
  for (int y = 0; y < SIM_GH; y++) {
    for (int x = 0; x < SIM_GW; x++) {
      int i = idx(x, y);
      double w = (s_mode == SIM_MODE_SAND) ? (s_mat[i] ? 1.0 : 0.0) : s_water[i] / 255.0;
      if (w <= 0) continue;
      sumX += x * w;
      sumY += y * w;
      weight += w;
    }
  }
  if (weight <= 0) {
    cx = cy = 0.5f;
    return;
  }
  cx = (float)(sumX / weight / (SIM_GW - 1));
  cy = (float)(sumY / weight / (SIM_GH - 1));
}

float simCentroidX() {
  float cx, cy;
  centroid(cx, cy);
  return cx;
}

float simCentroidY() {
  float cx, cy;
  centroid(cx, cy);
  return cy;
}

void simDumpAscii() {
  if (!s_mat) return;
  Serial.printf("--- %s grid %dx%d (4x2 cells per char) ---\n",
                s_mode == SIM_MODE_SAND ? "sand" : "water", SIM_GW, SIM_GH);
  for (int y = 0; y < SIM_GH; y += 4) {
    for (int x = 0; x < SIM_GW; x += 2) {
      int hits = 0;
      for (int dy = 0; dy < 4 && y + dy < SIM_GH; dy++) {
        for (int dx = 0; dx < 2 && x + dx < SIM_GW; dx++) {
          int i = idx(x + dx, y + dy);
          if (s_mode == SIM_MODE_SAND) {
            if (s_mat[i]) hits++;
          } else if (s_water[i] > 96) {
            hits++;
          }
        }
      }
      Serial.print(hits >= 4 ? '#' : (hits >= 1 ? '.' : ' '));
    }
    Serial.println();
  }
}
