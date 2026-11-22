// The split glass: the real face on the left, a data column on the right.
//
// The calendar event and the Concentration timer share this frame (S1 of
// `odd/tasks/temporizador-partido.md`). It was born as the calendar screen's
// private layout; when the timer needed the same shape, the geometry moved here
// so there is one definition instead of two copies that drift.
//
// Pure on purpose: no Arduino.h. The renderer in screen_buddy.cpp takes these
// numbers, and `tools/buddy_split_test.cpp` pins that the face (with its glow)
// never reaches the divider for any expression the pose ladders can produce.
#pragma once

#include <stdint.h>

#define BUDDY_SPLIT_W 320
#define BUDDY_SPLIT_H 172

// The one-pixel vertical rule. Everything left of it is the face; everything
// right of it is content. The rule keeps the calendar's shipped position.
#define BUDDY_SPLIT_DIVIDER_X 159
#define BUDDY_SPLIT_RULE_Y 10
#define BUDDY_SPLIT_RULE_H 152

// The content column. `W` leaves the same right margin the calendar had, so a
// panel of event text and the timer's digits are laid out against the same edge.
#define BUDDY_SPLIT_CONTENT_X 176
#define BUDDY_SPLIT_CONTENT_W (BUDDY_SPLIT_W - BUDDY_SPLIT_CONTENT_X - 8)

// The face keeps the full expression pipeline but a smaller frame: the pair is
// half scale around the pane's centre. The half gap is the *unscaled* screen
// distance from the pane centre to each eye, so the pair still reads as two eyes
// with a gap instead of one blob; the test proves the widest real expression
// stops short of the divider.
#define BUDDY_SPLIT_FACE_CX 79
#define BUDDY_SPLIT_FACE_CY 88
#define BUDDY_SPLIT_EYE_HALF_GAP 32
#define BUDDY_SPLIT_FACE_SCALE 0.50f
// Breathing room between the face's glow and either edge of the left pane.
#define BUDDY_SPLIT_FACE_MARGIN 2

// Where the two eye centres sit. The renderer scales the eye size and every
// gaze/turn offset by `scale`, so the pair keeps the full face's proportions.
struct BuddySplitFaceFrame {
  int cx[2];
  int cy;
  float scale;
};

static inline BuddySplitFaceFrame buddySplitFaceFrame() {
  BuddySplitFaceFrame f;
  f.cx[0] = BUDDY_SPLIT_FACE_CX - BUDDY_SPLIT_EYE_HALF_GAP;
  f.cx[1] = BUDDY_SPLIT_FACE_CX + BUDDY_SPLIT_EYE_HALF_GAP;
  f.cy = BUDDY_SPLIT_FACE_CY;
  f.scale = BUDDY_SPLIT_FACE_SCALE;
  return f;
}

// How far to slide both eyes so their glow stays inside the left pane. Called
// with the outer edges of the drawn pair (already including BUDDY_GLOW_PAD).
// A pair that already fits does not move; one that leaks off an edge is shifted
// back; one wider than the pane (which no real expression is) is centred.
static inline int buddySplitPairShift(int leftOuter, int rightOuter) {
  const int wantLeft = BUDDY_SPLIT_FACE_MARGIN - leftOuter;
  const int wantRight =
      (BUDDY_SPLIT_DIVIDER_X - BUDDY_SPLIT_FACE_MARGIN) - rightOuter;
  if (wantLeft > 0 && wantRight < 0) return (wantLeft + wantRight) / 2;
  if (wantLeft > 0) return wantLeft;
  if (wantRight < 0) return wantRight;
  return 0;
}
