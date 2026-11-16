// One button, two gestures: decide whether a tap stands alone or is the first
// half of a double tap.
//
// Pure and header-only so the firmware and `tools/tap_window_test.cpp` run the
// same code. A single tap starts the run, a double tap opens the calibration
// prompt, so the first tap cannot act immediately: it has to wait out a short
// window in case a second tap arrives. The window also has to survive the
// millis() wrap, which is why every comparison is a subtraction, not `<`.
#pragma once

#include <stdint.h>

// Long enough that two deliberate taps fit, short enough that a single tap does
// not feel delayed. 400 ms is the usual double-click threshold.
#define TAP_DOUBLE_MS 400

struct TapWindow {
  bool pending;      // a first tap is waiting to see if it is a double
  uint32_t firstAt;  // millis() of that first tap
};

enum TapResult {
  TAP_NONE,    // nothing to report; keep drawing
  TAP_WAIT,    // a first tap is still inside its window
  TAP_SINGLE,  // the window elapsed with no second tap
  TAP_DOUBLE,  // a second tap arrived inside the window
};

static inline void tapWindowReset(TapWindow &w) {
  w.pending = false;
  w.firstAt = 0;
}

// Registers a press. The first press opens the window and reports TAP_NONE; a
// press inside the window closes it and reports TAP_DOUBLE. A late press starts
// a new window instead of pairing with an expired one.
static inline TapResult tapWindowPress(TapWindow &w, uint32_t nowMs,
                                       uint32_t windowMs) {
  if (w.pending) {
    uint32_t elapsed = nowMs - w.firstAt;
    if (elapsed <= windowMs) {
      w.pending = false;
      return TAP_DOUBLE;
    }
  }
  w.pending = true;
  w.firstAt = nowMs;
  return TAP_NONE;
}

// Reports TAP_WAIT while a pending tap is still inside its window, TAP_SINGLE
// once the window elapsed, and TAP_NONE when nothing is pending. A single tap is
// reported exactly once.
static inline TapResult tapWindowPoll(TapWindow &w, uint32_t nowMs,
                                      uint32_t windowMs) {
  if (!w.pending) return TAP_NONE;
  uint32_t elapsed = nowMs - w.firstAt;
  if (elapsed < windowMs) return TAP_WAIT;
  w.pending = false;
  return TAP_SINGLE;
}
