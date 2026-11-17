// Sofía's face geometry, owner colour and motion smoothing, pure and
// header-only.
//
// Three things live here so `tools/buddy_face_test.cpp` can pin them without a
// board:
//
// - the owner's eye colour and the two derived shades (dim for offline, hot for
//   anger and dizziness);
// - the rounded-square (squircle) test that the renderer uses for every eye, so
//   the shape is one definition and not a pile of magic constants;
// - the pose filter that eases every continuous channel between frames. The
//   state ladders still decide the target; this decides how the face travels
//   there, which is what makes the whole thing feel smooth.
#pragma once

#include <math.h>
#include <stdint.h>

#include "buddy_anim.h"

// The cyan the owner saw and approved; also what the panel's reset restores.
#define BUDDY_EYE_COLOR_DEFAULT 0x00B4FFu
// Corner radius as a fraction of the eye's short half-axis. 0.38 keeps visible
// straight edges (a square, not a circle) while removing the hard corners.
#define BUDDY_EYE_CORNER_RATIO 0.38f
// How far the emotional tint pulls the eye towards red.
#define BUDDY_EYE_HOT_COLOR 0xFF3C1Eu
// Time constant of the pose easing. About 100 ms means a field covers ~63% of
// the distance in one 100 ms frame: quick enough to read as a reaction, slow
// enough that the 20 fps face never pops.
#define BUDDY_EASE_TAU_MS 100.0f

// Dark shade for the dim (unpaired/offline) face: dim but still the owner's
// hue, so the face keeps its identity when there is no bridge.
static inline uint32_t buddyEyeDim(uint32_t rgb) {
  uint32_t r = ((rgb >> 16) & 0xFFu) * 32u / 100u;
  uint32_t g = ((rgb >> 8) & 0xFFu) * 32u / 100u;
  uint32_t b = (rgb & 0xFFu) * 32u / 100u;
  return (r << 16) | (g << 8) | b;
}

static inline uint32_t buddyEyeHot(uint32_t rgb, float hot) {
  if (!(hot > 0.0f)) return rgb & 0xFFFFFFu;
  if (hot > 1.0f) hot = 1.0f;
  float br = (float)((rgb >> 16) & 0xFFu);
  float bg = (float)((rgb >> 8) & 0xFFu);
  float bb = (float)(rgb & 0xFFu);
  float tr = (float)((BUDDY_EYE_HOT_COLOR >> 16) & 0xFFu);
  float tg = (float)((BUDDY_EYE_HOT_COLOR >> 8) & 0xFFu);
  float tb = (float)(BUDDY_EYE_HOT_COLOR & 0xFFu);
  uint32_t r = (uint32_t)(br + (tr - br) * hot + 0.5f);
  uint32_t g = (uint32_t)(bg + (tg - bg) * hot + 0.5f);
  uint32_t b = (uint32_t)(bb + (tb - bb) * hot + 0.5f);
  return (r << 16) | (g << 8) | b;
}

// A rounded square described by its half extents and corner radius, measured
// from the eye centre.
struct BuddySquircle {
  float halfW;
  float halfH;
  float radius;
};

static inline BuddySquircle buddySquircle(float halfW, float halfH) {
  BuddySquircle s;
  s.halfW = halfW > 0.5f ? halfW : 0.5f;
  s.halfH = halfH > 0.5f ? halfH : 0.5f;
  float m = s.halfW < s.halfH ? s.halfW : s.halfH;
  s.radius = m * BUDDY_EYE_CORNER_RATIO;
  return s;
}

// True when (dx, dy), relative to the eye centre, is inside the rounded square.
// The renderer uses exactly this predicate, so a shape change is one edit.
static inline bool buddySquircleInside(float dx, float dy,
                                       const BuddySquircle &s) {
  float ax = fabsf(dx);
  float ay = fabsf(dy);
  if (ax > s.halfW || ay > s.halfH) return false;
  float qx = ax - (s.halfW - s.radius);
  float qy = ay - (s.halfH - s.radius);
  // One axis already inside the straight band: the point is inside, corners
  // only matter when both fall in the radius square.
  if (qx <= 0.0f || qy <= 0.0f) return true;
  return qx * qx + qy * qy <= s.radius * s.radius;
}

// Minimum gap in px between the drawn eye extents. Without it a wide or tilted
// pair touches in the middle and reads as one blob.
#define BUDDY_EYE_GAP_MIN 10

// Half extent in x of a rotated eye: what it actually occupies on screen.
static inline float buddyEyeExtentX(float halfW, float halfH, float deg) {
  float a = deg * 0.0174532925f;
  return fabsf(halfW * cosf(a)) + fabsf(halfH * sinf(a));
}

// How far to push each eye outwards so the pair keeps `minGap` px between their
// drawn extents. Returns 0 when they already fit.
static inline int buddyEyeGapPush(float centreDistance, float extentL,
                                  float extentR, float minGap) {
  float gap = centreDistance - (extentL + extentR);
  if (gap >= minGap) return 0;
  return (int)ceilf((minGap - gap) * 0.5f);
}

// Exponential approach towards the target. Never overshoots, so a value that
// stops changing settles exactly on it instead of vibrating around it.
static inline float buddyEase(float current, float target, uint32_t dtMs,
                              float tauMs) {
  if (dtMs == 0) return current;
  if (!(tauMs > 0.0f)) return target;
  float k = 1.0f - expf(-(float)dtMs / tauMs);
  if (k > 1.0f) k = 1.0f;
  float next = current + (target - current) * k;
  if ((target >= current && next > target) ||
      (target <= current && next < target)) {
    next = target;
  }
  return next;
}

// The eased pose channels. Booleans are not smoothed: an eyelid closes in a
// blink, and a morph between an arc and an open eye would look like rubber.
struct BuddyPoseFilter {
  bool seeded = false;
  float gazeX, gazeY, eyeGazeL, eyeGazeR;
  float rx, ry;
  float lid, lidL, lidR, bottom;
  float rot, rotL, rotR, hot, bob;
};

static inline void buddyPoseFilterReset(BuddyPoseFilter *f) {
  *f = BuddyPoseFilter();
}

// One step towards `target`. The first call after a reset adopts the target
// without easing: the face must not slide in from zero on its first frame.
static inline BuddyPose buddyPoseFilterStep(BuddyPoseFilter *f,
                                            const BuddyPose &target,
                                            uint32_t dtMs) {
  if (!f->seeded) {
    f->seeded = true;
    f->gazeX = target.gazeX;
    f->gazeY = target.gazeY;
    f->eyeGazeL = target.eyeGazeL;
    f->eyeGazeR = target.eyeGazeR;
    f->rx = (float)target.rx;
    f->ry = (float)target.ry;
    f->lid = target.lid;
    f->lidL = target.lidL;
    f->lidR = target.lidR;
    f->bottom = target.bottom;
    f->rot = target.rot;
    f->rotL = target.rotL;
    f->rotR = target.rotR;
    f->hot = target.hot;
    f->bob = target.bob;
    return target;
  }

  BuddyPose p = target;
  float tau = BUDDY_EASE_TAU_MS;
  f->gazeX = buddyEase(f->gazeX, target.gazeX, dtMs, tau);
  f->gazeY = buddyEase(f->gazeY, target.gazeY, dtMs, tau);
  f->eyeGazeL = buddyEase(f->eyeGazeL, target.eyeGazeL, dtMs, tau);
  f->eyeGazeR = buddyEase(f->eyeGazeR, target.eyeGazeR, dtMs, tau);
  f->rx = buddyEase(f->rx, (float)target.rx, dtMs, tau);
  f->ry = buddyEase(f->ry, (float)target.ry, dtMs, tau);
  f->lid = buddyEase(f->lid, target.lid, dtMs, tau);
  f->lidL = buddyEase(f->lidL, target.lidL, dtMs, tau);
  f->lidR = buddyEase(f->lidR, target.lidR, dtMs, tau);
  f->bottom = buddyEase(f->bottom, target.bottom, dtMs, tau);
  f->rot = buddyEase(f->rot, target.rot, dtMs, tau);
  f->rotL = buddyEase(f->rotL, target.rotL, dtMs, tau);
  f->rotR = buddyEase(f->rotR, target.rotR, dtMs, tau);
  f->hot = buddyEase(f->hot, target.hot, dtMs, tau);
  f->bob = buddyEase(f->bob, target.bob, dtMs, tau);

  p.gazeX = f->gazeX;
  p.gazeY = f->gazeY;
  p.eyeGazeL = f->eyeGazeL;
  p.eyeGazeR = f->eyeGazeR;
  p.rx = (int)lroundf(f->rx);
  p.ry = (int)lroundf(f->ry);
  p.lid = f->lid;
  p.lidL = f->lidL;
  p.lidR = f->lidR;
  p.bottom = f->bottom;
  p.rot = f->rot;
  p.rotL = f->rotL;
  p.rotR = f->rotR;
  p.hot = f->hot;
  p.bob = f->bob;
  return p;
}
