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

// Geometry of the band under the eyes, on the 320x172 glass. The face's lowest
// paint is y=144 at rest and y=156 in the widest surprise frame, so the band does
// not try to dodge it: while a session runs the timer owns a black plate from
// BUDDY_FOCUS_PLATE_Y down and draws on it. On a black glass the plate is
// invisible, except where it trims the bottom of a surprise glow by a few pixels
// for the length of that animation — cheaper than a number nobody can read.
#define BUDDY_FOCUS_PLATE_Y 146
#define BUDDY_FOCUS_BAND_Y 148
#define BUDDY_FOCUS_BAND_SCALE 2  // 5x7 font at 2x: 14 px tall, 12 px per digit
#define BUDDY_FOCUS_BAR_Y 166
#define BUDDY_FOCUS_BAR_H 5
#define BUDDY_FOCUS_BAR_W 120
#define BUDDY_FOCUS_LABEL_MAX 8

// The achievement colour: the glass's gold, the same one the splash uses.
#define BUDDY_FOCUS_GOLD 0xFFD700

// The bead at the end of a session. Not pure green: a touch of blue keeps it
// legible next to the eyes' cyan instead of reading as an alarm.
#define BUDDY_FOCUS_LED_R 0
#define BUDDY_FOCUS_LED_G 200
#define BUDDY_FOCUS_LED_B 60
#define BUDDY_FOCUS_LED_MS 2500u

// How the eyes behave while a session runs: once every period they drop towards
// the number and come back. Slow and regular, so it reads as studying.
#define BUDDY_FOCUS_DIP_PX 6
#define BUDDY_FOCUS_DIP_PERIOD_MS 9000u
#define BUDDY_FOCUS_DIP_START_MS 6000u

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

// How far the eyes drop towards the number, in pixels. Zero for most of the
// cycle, then a smooth triangle down and back: a glance at the work, not a
// twitch. Wrap-safe because it is a modulo of the clock.
static inline float buddyFocusGazeDip(uint32_t nowMs, bool studying) {
  if (!studying) return 0.0f;
  const uint32_t phase = nowMs % BUDDY_FOCUS_DIP_PERIOD_MS;
  if (phase < BUDDY_FOCUS_DIP_START_MS) return 0.0f;
  const uint32_t span = BUDDY_FOCUS_DIP_PERIOD_MS - BUDDY_FOCUS_DIP_START_MS;
  const uint32_t t = phase - BUDDY_FOCUS_DIP_START_MS;
  const float half = (float)span * 0.5f;
  const float k = (float)t < half ? (float)t / half
                                  : 1.0f - ((float)t - half) / half;
  return (float)BUDDY_FOCUS_DIP_PX * k;
}
