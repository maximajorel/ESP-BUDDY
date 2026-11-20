// The study countdown, pure and header-only.
//
// Concentración is a timer that lives on the SOFIA screen, under the eyes. This
// header owns only the arithmetic: when it started, how much is left, when it is
// finished. It knows nothing about the bridge, the mood, the face or the LED, and
// that is the point (S4): there is no phase or mood parameter anywhere below, so
// a bridge phase change or an /api/face mood cannot stop, restart or hide it.
//
// The remaining time is derived from an absolute deadline against millis(), never
// accumulated tick by tick. The face loop can stop for minutes (another screen,
// a stalled frame) and the countdown still tells the truth; the board clock is
// the only source of time. Every comparison is done on a signed difference so a
// countdown that crosses the 49.7-day millis() wrap keeps working.
//
// `tools/buddy_focus_test.cpp` pins all of it without a board.
#pragma once

#include <stdint.h>
#include <stdio.h>

#define BUDDY_FOCUS_DEFAULT_MINUTES 25u
#define BUDDY_FOCUS_MIN_MINUTES 1u
#define BUDDY_FOCUS_MAX_MINUTES 180u
// How long the achievement stays on the glass before the face goes back to
// normal. Long enough to be seen from across the room, short enough not to
// become furniture.
#define BUDDY_FOCUS_DONE_MS 6000u
#define BUDDY_FOCUS_MINUTE_MS 60000u

struct BuddyFocus {
  bool active = false;    // a countdown is running
  bool finished = false;  // the achievement is on screen
  uint32_t durationMs = 0;
  uint32_t endMs = 0;         // when it reaches zero
  uint32_t finishedAtMs = 0;  // when it reached zero, for the achievement window
};

static inline void buddyFocusClear(BuddyFocus *f) {
  if (!f) return;
  *f = BuddyFocus();
}

// Starts (or replaces) a countdown of `minutes`. Zero cancels and leaves the
// timer idle; above the maximum it clamps instead of rejecting, because a web
// field is a suggestion, not a contract.
static inline bool buddyFocusStart(BuddyFocus *f, uint32_t nowMs, uint16_t minutes) {
  if (!f) return false;
  if (minutes == 0) {
    buddyFocusClear(f);
    return true;
  }
  if (minutes > BUDDY_FOCUS_MAX_MINUTES) minutes = BUDDY_FOCUS_MAX_MINUTES;
  f->active = true;
  f->finished = false;
  f->durationMs = (uint32_t)minutes * BUDDY_FOCUS_MINUTE_MS;
  f->endMs = nowMs + f->durationMs;
  f->finishedAtMs = 0;
  return true;
}

static inline uint32_t buddyFocusRemainingMs(const BuddyFocus *f, uint32_t nowMs) {
  if (!f || !f->active) return 0;
  const int32_t left = (int32_t)(f->endMs - nowMs);
  return left > 0 ? (uint32_t)left : 0;
}

static inline bool buddyFocusDoneVisible(const BuddyFocus *f, uint32_t nowMs) {
  if (!f || !f->finished) return false;
  const int32_t since = (int32_t)(nowMs - f->finishedAtMs);
  return since >= 0 && since < (int32_t)BUDDY_FOCUS_DONE_MS;
}

// Advances the state machine. Called once per frame that draws the face; the
// only thing it does is notice that the deadline passed, and later that the
// achievement window did.
static inline void buddyFocusTick(BuddyFocus *f, uint32_t nowMs) {
  if (!f) return;
  if (f->active) {
    if ((int32_t)(f->endMs - nowMs) <= 0) {
      f->active = false;
      f->finished = true;
      f->finishedAtMs = nowMs;
    }
    return;
  }
  if (f->finished && (int32_t)(nowMs - (f->finishedAtMs + BUDDY_FOCUS_DONE_MS)) >= 0) {
    f->finished = false;
  }
}

// How much of the countdown is behind us, 0..255, for the progress mark under
// the number.
static inline uint8_t buddyFocusProgress(const BuddyFocus *f, uint32_t nowMs) {
  if (!f || f->durationMs == 0) return 0;
  if (!f->active) return f->finished ? 255 : 0;
  const uint32_t left = buddyFocusRemainingMs(f, nowMs);
  const uint32_t elapsed = f->durationMs - left;
  return (uint8_t)((elapsed * 255u) / f->durationMs);
}

// The label under the eyes: minutes and seconds, rounding a partial second up so
// the display never shows 00:00 while the countdown is still running.
static inline void buddyFocusFormat(uint32_t remainingMs, char *out, size_t cap) {
  if (!out || cap == 0) return;
  const uint32_t totalSec = (remainingMs + 999u) / 1000u;
  const uint32_t minutes = totalSec / 60u;
  const uint32_t seconds = totalSec % 60u;
  snprintf(out, cap, "%02u:%02u", (unsigned)minutes, (unsigned)seconds);
}
