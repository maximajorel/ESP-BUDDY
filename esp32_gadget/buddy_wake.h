// The power-on wake: the ladder that brings Sofía's face up out of black, pure
// and header-only.
//
// The design is `design/sofia-wake/01-amanecer.html` (artboard 01); this file is
// the firmware copy of its keyframe table, so the glass and the artboard cannot
// drift apart. The wake runs once, at boot, and ends on the neutral idle face:
// the energy rises from black, the eye slit opens to the full squircle, she
// blinks, gives a small shake of the eyes and puts on the happy squint.
//
// Every row carries every channel the wake drives, instead of only the fields it
// changes, because a row that inherits a value from a neighbour is exactly the
// kind of ambiguity that makes an animation impossible to pin. The seven
// channels are open, glow, grow, bob, gazeX, bottom and tremble; the rest of
// `BuddyPose` (lid, rot, tilt, hot, blink, happy, surprise, dim) stays at its
// struct defaults, so the wake can never inherit an expression.
//
// It is deliberately free of Arduino so `tools/buddy_wake_test.cpp` can pin the
// timeline, the easings and the neutral end pose without a board.
#pragma once

#include <stdint.h>

#include "buddy_anim.h"

// The whole wake, in ms. After this the normal idle face owns the glass.
#define BUDDY_WAKE_MS 5000

// The three curves the artboard's table names. Each segment uses the easing of
// the row it arrives at, which is how the artboard reads its KEYS array: the
// curve belongs to the keyframe being reached, not to the one being left.
enum BuddyWakeEase {
  BUDDY_WAKE_LINEAR = 0,
  BUDDY_WAKE_OUT,     // 1-(1-t)^2, a fast start that settles
  BUDDY_WAKE_IN_OUT,  // the artboard's easeInOut: half slow, half slow
};

static inline float buddyWakeEase(BuddyWakeEase kind, float t) {
  if (!(t > 0.0f)) return 0.0f;
  if (t > 1.0f) t = 1.0f;
  switch (kind) {
    case BUDDY_WAKE_OUT: return 1.0f - (1.0f - t) * (1.0f - t);
    case BUDDY_WAKE_IN_OUT: {
      float x = t * 2.0f;
      if (x < 1.0f) return x * x * 0.5f;
      return 1.0f - (2.0f - x) * (2.0f - x) * 0.5f;
    }
    case BUDDY_WAKE_LINEAR: break;
  }
  return t;
}

// One row of the S3 table, in ms order. `ease` is the curve of the segment that
// arrives at this row.
struct BuddyWakeKey {
  uint32_t at;
  BuddyWakeEase ease;
  float open;     // vertical aperture: 0 = the 4 px blink bar, 1 = the squircle
  float glow;     // the face's energy: the eye colour and its three halos
  float grow;     // eye scale
  float bob;      // vertical offset in px
  float gazeX;    // horizontal gaze in px
  float bottom;   // lower eyelid, the happy squint
  float tremble;  // 0..1 shake the renderer adds after the easing
};

// The artboard's timeline, one row per keyframe. `grow` starts at 0.25 and not at
// the artboard's 0.20 because `drawFace` treats grow <= 0.2 as 1.0 (the anger
// floor), so 0.20 would snap the eye to full width on the frame it crossed the
// clamp. With `glow` at 0 the first 100 ms are black anyway.
static const BuddyWakeKey kBuddyWakeKeys[] = {
    {0, BUDDY_WAKE_LINEAR, 0.0f, 0.0f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f},
    {900, BUDDY_WAKE_OUT, 0.05f, 0.5f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f},
    {2200, BUDDY_WAKE_IN_OUT, 1.07f, 1.0f, 1.02f, 0.0f, 0.0f, 0.0f, 0.0f},
    {2500, BUDDY_WAKE_OUT, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {2800, BUDDY_WAKE_IN_OUT, 0.03f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {3100, BUDDY_WAKE_OUT, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {3300, BUDDY_WAKE_IN_OUT, 1.0f, 1.0f, 1.0f, 1.2f, -2.0f, 0.0f, 0.0f},
    {3420, BUDDY_WAKE_OUT, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.85f},
    {3560, BUDDY_WAKE_LINEAR, 1.0f, 1.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.6f},
    {3680, BUDDY_WAKE_OUT, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {3800, BUDDY_WAKE_OUT, 1.0f, 1.0f, 1.08f, -3.0f, 0.0f, 0.32f, 0.0f},
    {4060, BUDDY_WAKE_IN_OUT, 1.0f, 1.0f, 1.06f, 0.8f, 0.0f, 0.34f, 0.0f},
    {4380, BUDDY_WAKE_IN_OUT, 1.0f, 1.0f, 1.04f, 0.0f, 0.0f, 0.30f, 0.0f},
    {4700, BUDDY_WAKE_IN_OUT, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.12f, 0.0f},
    {5000, BUDDY_WAKE_OUT, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
};

static const int kBuddyWakeKeyCount =
    (int)(sizeof(kBuddyWakeKeys) / sizeof(kBuddyWakeKeys[0]));

// Writes the seven wake channels of `*p` for `tMs` from the start of the wake,
// interpolating between the row pair that brackets the time with the arriving
// row's easing. Nothing else in the pose is touched, so the caller can start from
// a default `BuddyPose` and keep every expression channel at its default.
static inline void buddyWakePose(uint32_t tMs, BuddyPose *p) {
  if (!p) return;
  const BuddyWakeKey &last = kBuddyWakeKeys[kBuddyWakeKeyCount - 1];
  if (tMs >= last.at) {
    p->open = last.open;
    p->glow = last.glow;
    p->grow = last.grow;
    p->bob = last.bob;
    p->gazeX = last.gazeX;
    p->bottom = last.bottom;
    p->tremble = last.tremble;
    return;
  }
  int i = 0;
  while (i + 2 < kBuddyWakeKeyCount && tMs >= kBuddyWakeKeys[i + 1].at) i++;
  const BuddyWakeKey &a = kBuddyWakeKeys[i];
  const BuddyWakeKey &b = kBuddyWakeKeys[i + 1];
  float t = (float)(tMs - a.at) / (float)(b.at - a.at);
  float e = buddyWakeEase(b.ease, t);
  p->open = a.open + (b.open - a.open) * e;
  p->glow = a.glow + (b.glow - a.glow) * e;
  p->grow = a.grow + (b.grow - a.grow) * e;
  p->bob = a.bob + (b.bob - a.bob) * e;
  p->gazeX = a.gazeX + (b.gazeX - a.gazeX) * e;
  p->bottom = a.bottom + (b.bottom - a.bottom) * e;
  p->tremble = a.tremble + (b.tremble - a.tremble) * e;
}

// True once the wake has run its whole length and the idle face owns the glass.
static inline bool buddyWakeDone(uint32_t tMs) { return tMs >= BUDDY_WAKE_MS; }
