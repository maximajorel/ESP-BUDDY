// The owner's UI accent, shared by every screen and the panel.
//
// Stored as 0xRRGGBB in NVS (see settingsGetUiColor) and converted here for the
// framebuffer. The getter is cached in RAM, so calling it while drawing is free.
#pragma once

#include <stdint.h>

#include "gfx.h"
#include "settings.h"

static inline uint16_t uiAccent() { return gfxHex(settingsGetUiColor()); }

// A darker shade for secondary strokes (the slot sun's base, for example).
static inline uint16_t uiAccentDim() {
  uint32_t c = settingsGetUiColor();
  uint32_t r = ((c >> 16) & 0xFFu) * 3u / 4u;
  uint32_t g = ((c >> 8) & 0xFFu) * 3u / 4u;
  uint32_t b = (c & 0xFFu) * 3u / 4u;
  return gfxHex((r << 16) | (g << 8) | b);
}
