// The study countdown, pure and header-only.
//
// Concentración is a timer that lives on the SOFIA screen. Its countdown is the
// content column of the split frame (`buddy_split.h`); the real face keeps the
// left pane. This header owns only the arithmetic: when it started, how much is
// left, when it is finished, and how the eyes glance at it. It knows nothing
// about the bridge, the mood, the face or the LED, and that is the point (S4):
// there is no phase or mood parameter anywhere below, so a bridge phase change
// or an /api/face mood cannot stop, restart or hide it.
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
#define BUDDY_FOCUS_MIN_SECONDS 1u
// 99:59 is not an arbitrary ceiling: the number is four seven-segment digits
// wide on a 320 px glass, so a third minute digit would either shrink the whole
// timer or push it off the panel. The owner asked for the timer to be the
// biggest thing on the glass, and that decides the range.
#define BUDDY_FOCUS_MAX_SECONDS 5999u
#define BUDDY_FOCUS_MAX_MINUTES 99u
// How long the achievement stays on the glass before the face goes back to
// normal. Long enough to be seen from across the room, short enough not to
// become furniture.
#define BUDDY_FOCUS_DONE_MS 6000u
#define BUDDY_FOCUS_MINUTE_MS 60000u
#define BUDDY_FOCUS_SECOND_MS 1000u

// Geometry of the study layout, on the 320x172 glass. Concentración no longer
// owns the canvas: the countdown is the content column of the split frame
// (`buddy_split.h`), with the real face on the left. Only the column's internal
// geometry lives here, and `tools/buddy_focus_test.cpp` pins that the number and
// the label fit the column the split frame gives them.
//
//   y  12..19   label
//   y  48..112  the number: four digits plus a colon, 127 px of the 136 px column
//   y  69..90   LISTO, in the number's box, when the timer reaches zero

#define BUDDY_FOCUS_LABEL_TEXT "CONCENTRACION"
#define BUDDY_FOCUS_LABEL_Y 12
#define BUDDY_FOCUS_DIGIT_Y 48
#define BUDDY_FOCUS_DIGIT_W 27
#define BUDDY_FOCUS_DIGIT_H 64
#define BUDDY_FOCUS_DIGIT_THICK 6
#define BUDDY_FOCUS_DIGIT_GAP 2
#define BUDDY_FOCUS_COLON_W 9
#define BUDDY_FOCUS_COLON_GAP 3
// Four digits, two digit gaps, the colon and its two gaps: 127 px of the column.
#define BUDDY_FOCUS_DIGIT_TOTAL_W                            \
  (BUDDY_FOCUS_DIGIT_W * 4 + BUDDY_FOCUS_DIGIT_GAP * 2 +     \
   BUDDY_FOCUS_COLON_W + BUDDY_FOCUS_COLON_GAP * 2)
// The achievement is a word, not a number: five 5x7 glyphs at 3x.
#define BUDDY_FOCUS_DONE_SCALE 3
// Room for "MM:SS" plus the terminator. `buddyFocusFormat` never exceeds it.
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
#define BUDDY_FOCUS_DIP_PX 4
#define BUDDY_FOCUS_DIP_PERIOD_MS 9000u
#define BUDDY_FOCUS_DIP_START_MS 6000u

// Under five minutes the glance at the number comes twice as often, which is the
// only thing the glass does differently until the block is nearly over.
#define BUDDY_FOCUS_LAST_FIVE_MS 300000u
#define BUDDY_FOCUS_QUICK_DIP_PERIOD_MS 4500u

// The last minute: one 900 ms window where the face blinks twice and looks up at
// the owner instead of at the number. It is a window of the countdown, not a
// flag, so it cannot be missed and it cannot repeat.
#define BUDDY_FOCUS_FINAL_MS 60000u
#define BUDDY_FOCUS_FINAL_WINDOW_MS 900u
#define BUDDY_FOCUS_FINAL_GAZE_X (-10.0f)
#define BUDDY_FOCUS_FINAL_GAZE_Y (-5.0f)

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

// Starts (or replaces) a countdown of `seconds`, the unit of truth. Zero cancels
// and leaves the timer idle; above the maximum it clamps instead of rejecting,
// because a web field is a suggestion, not a contract.
static inline bool buddyFocusStartSeconds(BuddyFocus *f, uint32_t nowMs,
                                          uint32_t seconds) {
  if (!f) return false;
  if (seconds == 0) {
    buddyFocusClear(f);
    return true;
  }
  if (seconds > BUDDY_FOCUS_MAX_SECONDS) seconds = BUDDY_FOCUS_MAX_SECONDS;
  f->active = true;
  f->finished = false;
  f->durationMs = seconds * BUDDY_FOCUS_SECOND_MS;
  f->endMs = nowMs + f->durationMs;
  f->finishedAtMs = 0;
  return true;
}

// The minutes form is kept for every existing caller (the API, the console and
// the old tests). It is a wrapper, not a second state machine: the countdown is
// still stored in seconds, so a session started in minutes and one started in
// seconds cannot disagree. Minutes clamp to whole minutes first, so asking for
// 100 minutes lands on 99:00 and not on the odd 99:59 the seconds ceiling would
// give.
static inline bool buddyFocusStart(BuddyFocus *f, uint32_t nowMs,
                                   uint16_t minutes) {
  if (minutes > BUDDY_FOCUS_MAX_MINUTES) minutes = BUDDY_FOCUS_MAX_MINUTES;
  return buddyFocusStartSeconds(f, nowMs, (uint32_t)minutes * 60u);
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

// Whether the timer owns the glass right now and the face has to shrink into the
// top band. Pure: the draw tick asks this instead of inspecting the session.
static inline bool buddyFocusCompact(const BuddyFocus *f, uint32_t nowMs) {
  if (!f) return false;
  return f->active || buddyFocusDoneVisible(f, nowMs);
}

// How often the eyes visit the number. Under five minutes the visit comes twice
// as often: the block is nearly over and the clock is worth a look.
static inline uint32_t buddyFocusDipPeriodMs(uint32_t remainingMs) {
  if (remainingMs > 0u && remainingMs <= BUDDY_FOCUS_LAST_FIVE_MS) {
    return BUDDY_FOCUS_QUICK_DIP_PERIOD_MS;
  }
  return BUDDY_FOCUS_DIP_PERIOD_MS;
}

// True inside the 900 ms window that starts when the countdown crosses its last
// minute. Pure: it is a function of how much is left, so no flag has to be kept
// and a frozen frame cannot lose it.
static inline bool buddyFocusFinalMoment(uint32_t remainingMs) {
  return remainingMs <= BUDDY_FOCUS_FINAL_MS &&
         remainingMs + BUDDY_FOCUS_FINAL_WINDOW_MS > BUDDY_FOCUS_FINAL_MS;
}

// The two blinks inside that window, 300 ms apart: "falta poco".
static inline bool buddyFocusFinalBlink(uint32_t remainingMs) {
  if (!buddyFocusFinalMoment(remainingMs)) return false;
  const uint32_t in = BUDDY_FOCUS_FINAL_MS - remainingMs;
  return in < 130u || (in >= 300u && in < 430u);
}

// How far the eyes drop towards the number, in pixels. Zero for most of the
// cycle, then a smooth triangle down and back: a glance at the work, not a
// twitch. Wrap-safe because it is a modulo of the clock.
static inline float buddyFocusGazeDipFor(uint32_t nowMs, uint32_t remainingMs) {
  const uint32_t period = buddyFocusDipPeriodMs(remainingMs);
  const uint32_t phase = nowMs % period;
  const uint32_t start = period > BUDDY_FOCUS_DIP_START_MS
                             ? BUDDY_FOCUS_DIP_START_MS
                             : period / 2u;
  if (phase < start) return 0.0f;
  const uint32_t span = period - start;
  const uint32_t t = phase - start;
  const float half = (float)span * 0.5f;
  const float k = (float)t < half ? (float)t / half
                                  : 1.0f - ((float)t - half) / half;
  return (float)BUDDY_FOCUS_DIP_PX * k;
}

// The two-argument form the renderer and the tests already use: no session, no
// dip; a session with plenty of time left, the slow rhythm.
static inline float buddyFocusGazeDip(uint32_t nowMs, bool studying) {
  if (!studying) return 0.0f;
  return buddyFocusGazeDipFor(nowMs, BUDDY_FOCUS_LAST_FIVE_MS + 1u);
}
