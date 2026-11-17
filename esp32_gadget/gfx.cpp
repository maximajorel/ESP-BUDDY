#include "gfx.h"

#include <esp_heap_caps.h>

#include "buddy_face.h"
#include "font5x7.h"

static uint16_t *s_fb = nullptr;

bool gfxBegin() {
  s_fb = (uint16_t *)heap_caps_malloc(DISPLAY_PIXELS * 2, MALLOC_CAP_SPIRAM);
  if (!s_fb) return false;
  gfxClear(GFX_BLACK);
  return true;
}

uint16_t *gfxBuffer() { return s_fb; }

uint32_t gfxNowMs() { return (uint32_t)millis(); }

void gfxClear(uint16_t color) {
  if (!s_fb) return;
  // memset is much faster than a pixel loop for the two colours that are
  // actually uniform, which covers every screen's background.
  if (color == 0x0000 || color == 0xFFFF) {
    memset(s_fb, color & 0xFF, DISPLAY_PIXELS * 2);
    return;
  }
  for (int i = 0; i < DISPLAY_PIXELS; i++) s_fb[i] = color;
}

void gfxPixel(int x, int y, uint16_t color) {
  if (!s_fb) return;
  if (x < 0 || y < 0 || x >= DISPLAY_W || y >= DISPLAY_H) return;
  s_fb[(size_t)y * DISPLAY_W + x] = color;
}

void gfxFillRect(int x, int y, int w, int h, uint16_t color) {
  if (!s_fb || w <= 0 || h <= 0) return;
  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w;
  int y1 = y + h;
  if (x1 > DISPLAY_W) x1 = DISPLAY_W;
  if (y1 > DISPLAY_H) y1 = DISPLAY_H;
  if (x0 >= x1 || y0 >= y1) return;

  for (int row = y0; row < y1; row++) {
    uint16_t *dst = s_fb + (size_t)row * DISPLAY_W + x0;
    for (int col = x0; col < x1; col++) *dst++ = color;
  }
}

void gfxHLine(int x, int y, int w, uint16_t color) {
  gfxFillRect(x, y, w, 1, color);
}

void gfxVLine(int x, int y, int h, uint16_t color) {
  gfxFillRect(x, y, 1, h, color);
}

void gfxRect(int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  gfxHLine(x, y, w, color);
  gfxHLine(x, y + h - 1, w, color);
  gfxVLine(x, y, h, color);
  gfxVLine(x + w - 1, y, h, color);
}

void gfxLine(int x0, int y0, int x1, int y1, uint16_t color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    gfxPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void gfxFillCircle(int cx, int cy, int r, uint16_t color) {
  if (r <= 0) return;
  for (int dy = -r; dy <= r; dy++) {
    int span = (int)sqrtf((float)(r * r - dy * dy));
    gfxFillRect(cx - span, cy + dy, span * 2 + 1, 1, color);
  }
}

void gfxCircle(int cx, int cy, int r, uint16_t color) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    gfxPixel(cx + x, cy + y, color);
    gfxPixel(cx + y, cy + x, color);
    gfxPixel(cx - y, cy + x, color);
    gfxPixel(cx - x, cy + y, color);
    gfxPixel(cx - x, cy - y, color);
    gfxPixel(cx - y, cy - x, color);
    gfxPixel(cx + y, cy - x, color);
    gfxPixel(cx + x, cy - y, color);
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

// Even-odd scanline fill. The slot machine symbols are polygons, and drawing
// them from a vertex list keeps the source readable next to the sprite shape.
void gfxFillPolygon(const int *xs, const int *ys, int n, uint16_t color) {
  if (n < 3 || n > 16) return;

  int minY = ys[0], maxY = ys[0];
  for (int i = 1; i < n; i++) {
    if (ys[i] < minY) minY = ys[i];
    if (ys[i] > maxY) maxY = ys[i];
  }
  if (minY < 0) minY = 0;
  if (maxY > DISPLAY_H - 1) maxY = DISPLAY_H - 1;

  for (int y = minY; y <= maxY; y++) {
    int nodes[16];
    int count = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      bool crosses = (ys[i] < y && ys[j] >= y) || (ys[j] < y && ys[i] >= y);
      if (!crosses) continue;
      int dy = ys[j] - ys[i];
      if (dy == 0) continue;
      int x = xs[i] + (int)((long)(y - ys[i]) * (xs[j] - xs[i]) / dy);
      if (count < 16) nodes[count++] = x;
    }
    // Insertion sort: at most a handful of crossings per scanline.
    for (int a = 1; a < count; a++) {
      int v = nodes[a];
      int b = a - 1;
      while (b >= 0 && nodes[b] > v) {
        nodes[b + 1] = nodes[b];
        b--;
      }
      nodes[b + 1] = v;
    }
    for (int a = 0; a + 1 < count; a += 2) {
      int x0 = nodes[a], x1 = nodes[a + 1];
      if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
      }
      gfxFillRect(x0, y, x1 - x0 + 1, 1, color);
    }
  }
}

void gfxFillEllipse(int cx, int cy, int rx, int ry, uint16_t color) {
  if (rx <= 0 || ry <= 0) return;
  for (int dy = -ry; dy <= ry; dy++) {
    float k = 1.0f - (float)(dy * dy) / (float)(ry * ry);
    if (k < 0.0f) continue;
    int span = (int)(rx * sqrtf(k));
    gfxFillRect(cx - span, cy + dy, span * 2 + 1, 1, color);
  }
}

// Rotates the screen offset into shape space and asks the shared predicate. The
// row cross-section of a rounded square is convex under rotation, so each row is
// one span: sweep in from both sides instead of testing every pixel.
static bool squircleInsideRot(float dx, float dy, float ca, float sa,
                             const BuddySquircle &sq) {
  float lx = dx * ca + dy * sa;
  float ly = -dx * sa + dy * ca;
  return buddySquircleInside(lx, ly, sq);
}

void gfxFillSquircleRot(int cx, int cy, int halfW, int halfH, float deg,
                        uint16_t color) {
  if (!s_fb || halfW <= 0 || halfH <= 0) return;
  BuddySquircle sq = buddySquircle((float)halfW, (float)halfH);
  float a = deg * 0.0174532925f;
  float ca = cosf(a), sa = sinf(a);
  int ex = (int)(fabsf((float)halfW * ca) + fabsf((float)halfH * sa)) + 1;
  int ey = (int)(fabsf((float)halfW * sa) + fabsf((float)halfH * ca)) + 1;
  for (int dy = -ey; dy <= ey; dy++) {
    int y = cy + dy;
    if (y < 0 || y >= DISPLAY_H) continue;
    int first = ex + 1;
    for (int dx = -ex; dx <= ex; dx++) {
      if (squircleInsideRot((float)dx, (float)dy, ca, sa, sq)) {
        first = dx;
        break;
      }
    }
    if (first > ex) continue;
    int last = first;
    for (int dx = ex; dx > first; dx--) {
      if (squircleInsideRot((float)dx, (float)dy, ca, sa, sq)) {
        last = dx;
        break;
      }
    }
    gfxHLine(cx + first, y, last - first + 1, color);
  }
}

// Same shape, but the outline is dithered against the colour behind it. The
// panel has no alpha and no readable bus: a one-pixel hard edge is all a plain
// scanline fill can produce. Here the solid core is filled as spans and only the
// one-pixel ring around it pays for the distance maths, so the soft edge costs
// almost nothing per frame. Each boundary pixel keeps the foreground when the
// shape covers more than the 4x4 Bayer threshold, and the background colour
// otherwise: an ordered dither that reads as a smooth edge and never flickers
// between frames.
void gfxFillSquircleRotAA(int cx, int cy, int halfW, int halfH, float deg,
                          uint16_t color, uint16_t edgeColor) {
  if (!s_fb || halfW <= 0 || halfH <= 0) return;
  BuddySquircle outer = buddySquircle((float)halfW, (float)halfH);
  BuddySquircle core = buddySquircle((float)halfW - 1.6f, (float)halfH - 1.6f);
  float a = deg * 0.0174532925f;
  float ca = cosf(a), sa = sinf(a);
  int ex = (int)(fabsf((float)halfW * ca) + fabsf((float)halfH * sa)) + 2;
  int ey = (int)(fabsf((float)halfW * sa) + fabsf((float)halfH * ca)) + 2;

  // One boundary pixel: coverage picks between the shape and what is behind it.
  auto edge = [&](int px, int y, int dx, int dy) {
    if (px < 0 || px >= DISPLAY_W) return;
    float lx = (float)dx * ca + (float)dy * sa;
    float ly = -(float)dx * sa + (float)dy * ca;
    float coverage = buddySquircleCoverage(lx, ly, outer);
    if (coverage <= 0.0f) return;
    gfxPixel(px, y, buddyDitherLit(coverage, px, y) ? color : edgeColor);
  };

  for (int dy = -ey; dy <= ey; dy++) {
    int y = cy + dy;
    if (y < 0 || y >= DISPLAY_H) continue;
    int c0 = ex + 1;
    for (int dx = -ex; dx <= ex; dx++) {
      if (squircleInsideRot((float)dx, (float)dy, ca, sa, core)) {
        c0 = dx;
        break;
      }
    }
    int c1 = -ex - 1;
    for (int dx = ex; dx >= -ex; dx--) {
      if (squircleInsideRot((float)dx, (float)dy, ca, sa, core)) {
        c1 = dx;
        break;
      }
    }
    if (c0 <= c1) {
      gfxHLine(cx + c0, y, c1 - c0 + 1, color);
      for (int dx = -ex; dx < c0; dx++) edge(cx + dx, y, dx, dy);
      for (int dx = c1 + 1; dx <= ex; dx++) edge(cx + dx, y, dx, dy);
    } else {
      // A row thinner than the core: every pixel of it is boundary.
      for (int dx = -ex; dx <= ex; dx++) edge(cx + dx, y, dx, dy);
    }
  }
}

static const uint8_t *glyphFor(char ch) {
  uint8_t code = (uint8_t)ch;
  if (code < FONT_FIRST || code > FONT_LAST) return FONT_FALLBACK;
  return FONT_GLYPHS[code - FONT_FIRST];
}

void gfxChar(int x, int y, int scale, char ch, uint16_t color) {
  if (!s_fb || scale < 1) return;
  const uint8_t *glyph = glyphFor(ch);
  for (int c = 0; c < FONT_W; c++) {
    uint8_t column = glyph[c];
    for (int r = 0; r < FONT_H; r++) {
      if (column & (1 << r)) {
        gfxFillRect(x + c * scale, y + r * scale, scale, scale, color);
      }
    }
  }
}

int gfxGlyphIndex(char ch) {
  const uint8_t *glyph = glyphFor(ch);
  ptrdiff_t bytes = glyph - FONT_GLYPHS[0];
  ptrdiff_t rows = bytes / FONT_W;
  if (rows < 0 || rows >= (ptrdiff_t)gfxFontRows()) return -1;
  return (int)rows;
}

const uint8_t *gfxGlyphBytes(char ch) { return glyphFor(ch); }
int gfxFontFirst() { return FONT_FIRST; }
int gfxFontLast() { return FONT_LAST; }
int gfxFontRows() { return (int)(sizeof(FONT_GLYPHS) / FONT_W); }

// Devuelve el numero de filas impresas, para que la consola sepa cuando parar.
int gfxFontDumpRows(int from, int to) {
  int rows = gfxFontRows();
  if (from < 0) from = 0;
  if (to >= rows) to = rows - 1;
  int n = 0;
  for (int i = from; i <= to; i++) {
    const uint8_t *g = FONT_GLYPHS[i];
    Serial.printf("row %3d code %3d '%c' %02x %02x %02x %02x %02x\n", i,
                  FONT_FIRST + i,
                  (FONT_FIRST + i >= 32 && FONT_FIRST + i < 127) ? FONT_FIRST + i : '?',
                  g[0], g[1], g[2], g[3], g[4]);
    n++;
  }
  return n;
}

int gfxTextWidth(int scale, const char *text) {  if (!text || !*text) return 0;
  return (int)strlen(text) * (FONT_W + 1) * scale - scale;
}

void gfxText(int x, int y, int scale, const char *text, uint16_t color) {
  if (!text) return;
  int cx = x;
  int advance = (FONT_W + 1) * scale;
  for (const char *p = text; *p; p++) {
    if (*p != ' ') gfxChar(cx, y, scale, *p, color);
    cx += advance;
  }
}

void gfxTextCentered(int y, int scale, const char *text, uint16_t color) {
  int w = gfxTextWidth(scale, text);
  gfxText((DISPLAY_W - w) / 2, y, scale, text, color);
}

// Segment bits for a seven-segment digit:
//   --a--      a top, b top-right, c bottom-right, d bottom,
//   f| b |      e bottom-left, f top-left, g middle.
//   --g--
//   e| c |
//   --d--
static const uint8_t SEGMENTS[10] = {
    0x3F,  // 0: a b c d e f
    0x06,  // 1: b c
    0x5B,  // 2: a b d e g
    0x4F,  // 3: a b c d g
    0x66,  // 4: b c f g
    0x6D,  // 5: a c d f g
    0x7D,  // 6: a c d e f g
    0x07,  // 7: a b c
    0x7F,  // 8: all
    0x6F,  // 9: a b c d f g
};

// Draws one segment as a bar with mitred ends, which is what makes a
// seven-segment display read as a display and not as seven separate blocks.
static void hSegment(int x, int y, int length, int thickness, uint16_t color) {
  int t = thickness;
  gfxFillRect(x, y, length, t, color);
  gfxFillRect(x, y + t / 2, t / 2 + 1, t - t / 2, color);
  gfxFillRect(x + length - t / 2 - 1, y + t / 2, t / 2 + 1, t - t / 2, color);
}

static void vSegment(int x, int y, int length, int thickness, uint16_t color) {
  int t = thickness;
  gfxFillRect(x, y, t, length, color);
  gfxFillRect(x + t / 2, y, t - t / 2, t / 2 + 1, color);
  gfxFillRect(x + t / 2, y + length - t / 2 - 1, t - t / 2, t / 2 + 1, color);
}

void gfxSegmentDigit(int x, int y, int w, int h, int thickness, char digit,
                     uint16_t color, uint16_t dimColor) {
  uint8_t mask = 0;
  if (digit >= '0' && digit <= '9') mask = SEGMENTS[digit - '0'];

  int t = thickness;
  int vLen = (h - 3 * t) / 2;  // vertical segments split the remaining height
  int hLen = w;
  int midY = y + t + vLen;

  // Unlit segments are drawn very dim, the way a real LED display shows them.
  for (int pass = 0; pass < 2; pass++) {
    bool lit = (pass == 1);
    uint16_t c = lit ? color : dimColor;
    if (!lit && dimColor == color) continue;

    // a (top), d (bottom), g (middle) are horizontal.
    if (!lit || (mask & 0x01)) hSegment(x, y, hLen, t, c);
    if (!lit || (mask & 0x08)) hSegment(x, y + h - t, hLen, t, c);
    if (!lit || (mask & 0x40)) hSegment(x, midY, hLen, t, c);
    // f (top-left), b (top-right), e (bottom-left), c (bottom-right).
    if (!lit || (mask & 0x20)) vSegment(x, y + t / 2, vLen + t / 2, t, c);
    if (!lit || (mask & 0x02)) vSegment(x + w - t, y + t / 2, vLen + t / 2, t, c);
    if (!lit || (mask & 0x10)) vSegment(x, midY + t / 2, vLen + t / 2, t, c);
    if (!lit || (mask & 0x04)) vSegment(x + w - t, midY + t / 2, vLen + t / 2, t, c);
  }
}

void gfxSegmentColon(int x, int y, int w, int h, int thickness, uint16_t color) {
  int size = thickness;
  gfxFillRect(x, y + h / 3 - size / 2, size, size, color);
  gfxFillRect(x, y + 2 * h / 3 - size / 2, size, size, color);
}
