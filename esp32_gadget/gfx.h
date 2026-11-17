// Software drawing layer over a 320x172 RGB565 framebuffer.
//
// The panel has no readable bus (MISO is not connected), so nothing can be
// verified by reading pixels back. Everything therefore draws into one
// framebuffer that the firmware owns: the serial self-test can inspect it, and
// the display only ever receives a finished frame.
#pragma once

#include <Arduino.h>

#include "display.h"

// RGB888 -> RGB565. The high-byte-first swap the panel needs happens in
// displayPush(), so colours here stay in host order.
static inline uint16_t gfxColor(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 24-bit hex literal to RGB565, handy next to the web UI's CSS colours.
static inline uint16_t gfxHex(uint32_t rgb) {
  return gfxColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

#define GFX_BLACK 0x0000
#define GFX_WHITE 0xFFFF

// Returns false when the framebuffer cannot be allocated.
bool gfxBegin();
uint16_t *gfxBuffer();
void gfxClear(uint16_t color);

void gfxPixel(int x, int y, uint16_t color);
void gfxFillRect(int x, int y, int w, int h, uint16_t color);
void gfxRect(int x, int y, int w, int h, uint16_t color);
void gfxHLine(int x, int y, int w, uint16_t color);
void gfxVLine(int x, int y, int h, uint16_t color);
void gfxLine(int x0, int y0, int x1, int y1, uint16_t color);
void gfxFillCircle(int cx, int cy, int r, uint16_t color);
void gfxCircle(int cx, int cy, int r, uint16_t color);

// Even-odd scanline fill. Used for the slot machine symbols, which are polygons
// rather than rectangles or circles. At most 16 vertices.
void gfxFillPolygon(const int *xs, const int *ys, int n, uint16_t color);
// Ellipse outline/fill, for the lemon symbol and round UI accents.
void gfxFillEllipse(int cx, int cy, int rx, int ry, uint16_t color);
// Filled rounded square (squircle) with half extents, rotated by `deg`, used by
// Sofía's eyes. The corner ratio lives in buddy_face.h so the shape and its
// test share one definition.
void gfxFillSquircleRot(int cx, int cy, int halfW, int halfH, float deg,
                        uint16_t color);
// The same rounded square with a dithered one-pixel outline: boundary pixels
// alternate between `color` and the `edgeColor` behind it according to their
// coverage and a 4x4 Bayer threshold. This is how a panel without alpha gets a
// soft edge and a glow that fades instead of stepping.
void gfxFillSquircleRotAA(int cx, int cy, int halfW, int halfH, float deg,
                          uint16_t color, uint16_t edgeColor);

// Text: every glyph sits in a 6x8 cell scaled by `scale` (5x7 ink plus a
// one-pixel gap). Both cases exist in the font, because the network screen has
// to print a WiFi password exactly as stored.
int gfxTextWidth(int scale, const char *text);

// Debug hooks: which glyph the renderer actually resolves for `ch`, and its
// five column bytes. Used by the `fonttest` console command, which is how a
// font/lookup problem is told apart from corrupt data without any network.
int gfxGlyphIndex(char ch);
const uint8_t *gfxGlyphBytes(char ch);
int gfxFontFirst();
int gfxFontLast();
int gfxFontRows();
// Prints compiled rows [from, to] and returns how many. Console diagnostics.
int gfxFontDumpRows(int from, int to);void gfxText(int x, int y, int scale, const char *text, uint16_t color);
void gfxTextCentered(int y, int scale, const char *text, uint16_t color);
void gfxChar(int x, int y, int scale, char ch, uint16_t color);
// Seven-segment digit, for the clock. `w`/`h` are the overall digit box and
// `thickness` the stroke width of each segment.
void gfxSegmentDigit(int x, int y, int w, int h, int thickness, char digit,
                     uint16_t color, uint16_t dimColor);
void gfxSegmentColon(int x, int y, int w, int h, int thickness, uint16_t color);

// Frame timing helper shared by the screens.
uint32_t gfxNowMs();
