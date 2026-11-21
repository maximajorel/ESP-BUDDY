// Value rules for the CONFIG screen, pure and header-only.
//
// Two buttons walk each brightness, so the arithmetic is the behaviour: a step
// must land on the ceiling and stay there, because a wrap landed on the LCD's
// 5% floor and the owner read that as a screen that had turned off; the step
// back must stop at its own floor the same way; a value stored by some other
// firmware is repaired to the nearest legal end in one press instead of being
// stepped from a number nobody can see; and the bead's 0..255 level has to
// survive a trip through the percentage the glass and the panel both show.
// Pinned by `tools/config_value_test.cpp`.
#pragma once

#include <stdint.h>

// The LCD floor matches the web panel's slider: below 5% the picture is
// unreadable, and a screen you cannot read cannot be turned back up.
#define CFG_LCD_MIN 5
#define CFG_LCD_MAX 100
#define CFG_LCD_STEP 10

// The bead may go fully dark; it is not the thing the owner is looking at.
#define CFG_LED_MIN 0
#define CFG_LED_MAX 100
#define CFG_LED_STEP 10

static inline uint8_t cfgClamp(int value, uint8_t min, uint8_t max) {
  if (value < (int)min) return min;
  if (value > (int)max) return max;
  return (uint8_t)value;
}

// The LCD percent, from any caller. The panel's slider, the CONFIG screen and the
// serial console all reach brightness through storage, and the HTTP transport in
// front of them used to clamp to a 0..255 range that exists nowhere else in the
// firmware: `settingsSetLcdBrightness` then clamped again to 5..100. Two ranges
// for one value is how a request for 200% silently became 100% while the code
// read as if it were valid. This is the only clamp that decides what a percent
// is, and it takes an int so a caller never has to cast first.
static inline uint8_t cfgClampLcd(int percent) {
  return cfgClamp(percent, CFG_LCD_MIN, CFG_LCD_MAX);
}

// Next value in one step up. A value outside the range (a corrupt store, a
// firmware that shipped a different scale) is repaired to the nearest legal end
// rather than stepped, so a single press always brings it back into the range the
// owner can see. A step that would cross the maximum lands on it exactly and the
// next press keeps it there: a wrap would drop the LCD to 5% and look like the
// screen had turned off.
static inline uint8_t cfgStepUp(uint8_t current, uint8_t min, uint8_t max,
                                uint8_t step) {
  if (min > max) return min;
  if (current < min) return min;
  if (current >= max) return max;
  if (step == 0) return current;
  uint16_t next = (uint16_t)current + step;
  if (next > max) return max;
  return (uint8_t)next;
}

// The same rule downwards: never below the floor, and the press after landing on
// it keeps it there. The LED's floor is 0 (fully dark is a value the owner
// chose), the LCD's is 5 (a screen nobody can read cannot be turned back up).
static inline uint8_t cfgStepDown(uint8_t current, uint8_t min, uint8_t max,
                                  uint8_t step) {
  if (min > max) return min;
  if (current > max) return max;
  if (current <= min) return min;
  if (step == 0) return current;
  if ((uint16_t)step >= (uint16_t)(current - min)) return min;
  return (uint8_t)(current - step);
}

// 0..100 percent onto the bead's 0..255 level, rounded, so 100% is really 255.
static inline uint8_t cfgPercentToLevel(uint8_t percent) {
  if (percent > 100) percent = 100;
  return (uint8_t)(((uint16_t)percent * 255u + 50u) / 100u);
}

// The reverse, rounded: level 255 reads as 100% and the eleven steps the screen
// can select all come back unchanged.
static inline uint8_t cfgLevelToPercent(uint8_t level) {
  return (uint8_t)(((uint16_t)level * 100u + 127u) / 255u);
}
