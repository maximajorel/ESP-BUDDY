// UI accent colour parsing and formatting, pure and header-only.
//
// The panel sends a CSS hex colour ("#7C4DFF" or "7c4dff"); the device stores
// 0xRRGGBB in NVS and reports it back as "#RRGGBB" in /api/status. Keeping the
// two directions in one tested place avoids a colour that only looks right in
// one of the two UIs.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static inline int uiHexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Accepts "#RRGGBB" or "RRGGBB" with either case. Returns false for anything
// else, so a typo in the panel can never reach the settings store.
static inline bool uiColorParse(const char *text, uint32_t *out) {
  if (!text || !out) return false;
  if (*text == '#') text++;
  if (strlen(text) != 6) return false;
  uint32_t value = 0;
  for (int i = 0; i < 6; i++) {
    int digit = uiHexDigit(text[i]);
    if (digit < 0) return false;
    value = (value << 4) | (uint32_t)digit;
  }
  *out = value;
  return true;
}

// Formats 0xRRGGBB as "#RRGGBB" into an 8-byte buffer.
static inline void uiColorToHex(uint32_t rgb, char out[8]) {
  snprintf(out, 8, "#%06lX", (unsigned long)(rgb & 0xFFFFFFu));
}
