// A steady colour that outranks the bead's base colour for a while, pure.
//
// The pixel already has three layers (jackpot celebration > pulse fade > base
// colour). Neither of the two top layers can say "steadily green for two and a
// half seconds": the celebration strobes casino colours and the pulse fades from
// full to nothing. A finished study session needs exactly that steady hold, and
// it has to survive the buddy's per-loop base colour, which repaints the pixel
// every pass (buddy_led.cpp). Hence a fourth layer with its own tiny state and
// its own host test, instead of a new branch inside rgbTick.
#pragma once

#include <stdint.h>

// A hold is an acknowledgement, not a signal lamp: anything longer than a minute
// is a bug in the caller, so it clamps instead of running forever.
#define RGB_HOLD_MS_MAX 60000u

struct RgbHold {
  bool active = false;
  uint32_t endMs = 0;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

static inline void rgbHoldClear(RgbHold *h) {
  if (!h) return;
  *h = RgbHold();
}

static inline void rgbHoldStart(RgbHold *h, uint32_t nowMs, uint8_t r, uint8_t g,
                                uint8_t b, uint32_t ms) {
  if (!h) return;
  if (ms == 0) {
    rgbHoldClear(h);
    return;
  }
  if (ms > RGB_HOLD_MS_MAX) ms = RGB_HOLD_MS_MAX;
  h->active = true;
  h->endMs = nowMs + ms;
  h->r = r;
  h->g = g;
  h->b = b;
}

// True while the hold still owns the pixel. Expiry is a signed difference, so a
// hold that crosses the millis() wrap ends on time instead of lasting forever.
static inline bool rgbHoldStep(RgbHold *h, uint32_t nowMs) {
  if (!h || !h->active) return false;
  if ((int32_t)(h->endMs - nowMs) <= 0) {
    h->active = false;
    return false;
  }
  return true;
}
