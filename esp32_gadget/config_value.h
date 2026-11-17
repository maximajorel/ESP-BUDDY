// Value rules for the CONFIG screen, pure and header-only.
//
// One button has to walk two brightnesses, so the arithmetic is the behaviour:
// the step must be able to land on the maximum, the wrap must fall back to each
// value's own floor (the LCD cannot go to 0 or the owner could not see what they
// are doing), and the bead's 0..255 level has to survive a trip through the
// percentage the glass and the panel both show. Pinned by
// `tools/config_value_test.cpp`.
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

// Next value in one step. A value outside the range (a corrupt store, a firmware
// that shipped a different scale) is read as the floor rather than stepped, so a
// single tap always brings it back into the selectable range. A step that would
// cross the maximum lands on it exactly, and the step after that wraps to the
// floor: with a floor of 5 and a step of 10 the walk would otherwise never reach
// 100.
static inline uint8_t cfgStepUp(uint8_t current, uint8_t min, uint8_t max,
                                uint8_t step) {
  if (min > max) return min;
  if (current < min || current > max) return min;
  if (current >= max) return min;
  if (step == 0) return current;
  uint16_t next = (uint16_t)current + step;
  if (next > max) return max;
  return (uint8_t)next;
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
