// When should the glass and the LED bead go dark?
//
// The display auto-off used to not exist at all, and now that it does the rule is
// the kind that is invisible until it misfires: a clock that never sleeps, a
// game that sleeps mid-run, or a timer that fires right after the millis() wrap.
// Pure and header-only, pinned by `tools/screen_sleep_test.cpp`.
#pragma once

#include <stdint.h>

// Longest selectable timeout, in seconds (one hour).
#define SCREEN_SLEEP_MAX_SEC 3600u

// A mask bit per screen id; ids past the first eight cannot be included.
static inline bool screenSleepIncluded(uint8_t mask, uint8_t screen) {
  return screen < 8 && ((mask >> screen) & 1u) != 0;
}

// seconds == 0 means "never". The subtraction makes the comparison safe across
// the millis() wrap; the multiplication stays far below the uint32 limit because
// the longest timeout is one hour.
//
// `lastActivityMs` can also be a few milliseconds *ahead* of `nowMs`, and that is
// not a wrap: loop() reads the clock into `now`, and then the button, console and
// network handlers stamp their own, later millis() before the frame is drawn. The
// unsigned subtraction would read that as ~49 days idle and put the glass out on
// the spot, which is exactly how CONFIG used to go dark right after a tap.
static inline bool screenSleepDue(uint32_t nowMs, uint32_t lastActivityMs,
                                  uint16_t seconds) {
  if (seconds == 0) return false;
  if ((int32_t)(nowMs - lastActivityMs) < 0) return false;
  return (uint32_t)(nowMs - lastActivityMs) >= (uint32_t)seconds * 1000u;
}
