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
// How far the black lid band reaches past the eye and its glow, so a rotated
// eye can never show a hot sliver above the lid.
#define BUDDY_GLOW_PAD 6

// Time constant of the pose easing. About 100 ms means a field covers ~63% of
// the distance in one 100 ms frame: quick enough to read as a reaction, slow
// enough that the 20 fps face never pops.
#define BUDDY_EASE_TAU_MS 100.0f

// Scales every channel by `percent`, keeping the hue. Used for the dim face
// and for the two glow layers around the eyes.
static inline uint32_t buddyEyeScale(uint32_t rgb, int percent) {
  if (percent <= 0) return 0x000000u;
  if (percent > 100) percent = 100;
  uint32_t r = ((rgb >> 16) & 0xFFu) * (uint32_t)percent / 100u;
  uint32_t g = ((rgb >> 8) & 0xFFu) * (uint32_t)percent / 100u;
  uint32_t b = (rgb & 0xFFu) * (uint32_t)percent / 100u;
  return (r << 16) | (g << 8) | b;
}

// Dark shade for the dim (unpaired/offline) face: dim but still the owner's
// hue, so the face keeps its identity when there is no bridge.
static inline uint32_t buddyEyeDim(uint32_t rgb) {
  return buddyEyeScale(rgb, 32);
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

// Signed distance to the rounded-square edge in px: negative inside, zero on
// the edge, positive outside. This is what turns a hard one-pixel edge into a
// coverage value the renderer can dither.
static inline float buddySquircleSdf(float dx, float dy,
                                     const BuddySquircle &s) {
  float qx = fabsf(dx) - (s.halfW - s.radius);
  float qy = fabsf(dy) - (s.halfH - s.radius);
  float ox = qx > 0.0f ? qx : 0.0f;
  float oy = qy > 0.0f ? qy : 0.0f;
  float inside = qx > qy ? qx : qy;
  if (inside > 0.0f) inside = 0.0f;
  return sqrtf(ox * ox + oy * oy) + inside - s.radius;
}

// How much of one pixel the shape covers: 1 well inside, 0 outside, fractional
// within half a pixel of the edge.
static inline float buddySquircleCoverage(float dx, float dy,
                                         const BuddySquircle &s) {
  float c = 0.5f - buddySquircleSdf(dx, dy, s);
  if (c < 0.0f) return 0.0f;
  if (c > 1.0f) return 1.0f;
  return c;
}

// Ordered 4x4 Bayer threshold in [0, 1). The panel has no alpha, so a partial
// coverage becomes a mixture of the two colours a screen pixel can hold; the
// fixed pattern keeps that mixture stable instead of noisy frame to frame.
static inline float buddyBayer4(int x, int y) {
  static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6,
                                     3, 11, 1, 9, 15, 7, 13, 5};
  return (float)kBayer[(y & 3) * 4 + (x & 3)] / 16.0f;
}

// True when this pixel shows the foreground colour at `coverage`.
static inline bool buddyDitherLit(float coverage, int x, int y) {
  return coverage > buddyBayer4(x, y);
}

// --- perspective --------------------------------------------------------------
//
// Looking sideways is a small turn of the head, not a slide of a decal: the eye
// the gaze leads with travels a little further and foreshortens (it turns away
// from the viewer), the trailing eye widens. Two pixels and a 6% squeeze; the
// brain reads it as depth and the eye never looks pasted on.
struct BuddyPerspective {
  float pushL = 0;
  float pushR = 0;
  float widenL = 1.0f;
  float widenR = 1.0f;
};

#define BUDDY_PERSPECTIVE_PX 2.0f
#define BUDDY_PERSPECTIVE_GAZE 18.0f  // gaze px that counts as a full look

static inline BuddyPerspective buddyPerspective(float gazeX) {
  BuddyPerspective p;
  float t = gazeX / BUDDY_PERSPECTIVE_GAZE;
  if (t > 1.0f) t = 1.0f;
  if (t < -1.0f) t = -1.0f;
  float mag = t < 0.0f ? -t : t;
  if (t > 0.0f) {
    p.pushR = BUDDY_PERSPECTIVE_PX * t;
    p.pushL = 0.4f * BUDDY_PERSPECTIVE_PX * t;
    p.widenR = 1.0f - 0.06f * mag;
    p.widenL = 1.0f + 0.03f * mag;
  } else if (t < 0.0f) {
    p.pushL = BUDDY_PERSPECTIVE_PX * t;
    p.pushR = 0.4f * BUDDY_PERSPECTIVE_PX * t;
    p.widenL = 1.0f - 0.06f * mag;
    p.widenR = 1.0f + 0.03f * mag;
  }
  return p;
}

// Where the eyelid line lands on screen at `dx` px from the eye centre. The lid
// is parallel to the eye's top edge, so a rotated eye gets a slanted lid: with a
// positive rotation the line falls to the right ("\"), with a negative one it
// rises ("/"). Angry eyes rotate opposite ways, which is what draws the "\/"
// the owner asked for.
static inline float buddyLidScreenY(int cy, int halfH, float lid, float deg,
                                    float dx) {
  float a = deg * 0.0174532925f;
  float y0 = -(float)halfH + 2.0f * (float)halfH * lid;
  return (float)cy + dx * tanf(a) + y0 / cosf(a);
}

// --- tremble ------------------------------------------------------------------
//
// Anger shakes. The pose filter is there to smooth intent, so it would eat a
// tremor; this is noise applied after the ease instead. 25 Hz keeps it reading
// as shaking rather than as jitter between two frames.
static inline float buddyTremble(uint32_t nowMs, uint32_t seed, uint32_t salt,
                                 float amp) {
  if (!(amp > 0.0f)) return 0.0f;
  uint32_t frame = nowMs / 40u;
  return (buddyUnit(seed, frame * 31u + salt) * 2.0f - 1.0f) * amp;
}

// Minimum gap in px between the drawn eye extents. Without it a wide or tilted
// pair touches in the middle and reads as one blob.
#define BUDDY_EYE_GAP_MIN 10

// Half extent in x of a rotated eye: what it actually occupies on screen.
static inline float buddyEyeExtentX(float halfW, float halfH, float deg) {
  float a = deg * 0.0174532925f;
  return fabsf(halfW * cosf(a)) + fabsf(halfH * sinf(a));
}

// Half extent in y of a rotated eye; the vertical mirror of buddyEyeExtentX.
static inline float buddyEyeExtentY(float halfW, float halfH, float deg) {
  return buddyEyeExtentX(halfH, halfW, deg);
}

// How far to push each eye outwards so the pair keeps `minGap` px between their
// drawn extents. Returns 0 when they already fit.
static inline int buddyEyeGapPush(float centreDistance, float extentL,
                                  float extentR, float minGap) {
  float gap = centreDistance - (extentL + extentR);
  if (gap >= minGap) return 0;
  return (int)ceilf((minGap - gap) * 0.5f);
}

// A black lid band, in screen coordinates: where it starts and how tall it is.
struct BuddyLidBand {
  int y0;
  int h;
};

// The bottom lid is the same band mirrored: it starts at the bottom lid line
// and covers down past the rotated extent and the glow.
static inline BuddyLidBand buddyLidBandBottom(int cy, int extentY, int pad,
                                              float bottom) {
  BuddyLidBand b;
  int end = cy + extentY + pad + 2;
  if (!(bottom > 0.02f)) {
    b.y0 = end;
    b.h = 0;
    return b;
  }
  if (bottom > 1.0f) bottom = 1.0f;
  b.y0 = cy + extentY - (int)(2.0f * (float)extentY * bottom);
  b.h = end - b.y0 + 1;
  if (b.h < 1) b.h = 1;
  return b;
}

static inline float buddyLidLineBottom(int cy, int extentY, float bottom) {
  if (bottom < 0.0f) bottom = 0.0f;
  if (bottom > 1.0f) bottom = 1.0f;
  return (float)cy + (float)extentY - 2.0f * (float)extentY * bottom;
}

// Half extent in y of the drawn eye. `grow` already decides how tall the eye is;
// `open` is the aperture that carries the eye from a shut 4 px bar to that full
// squircle, which is what lets the boot wake open a slit into a face and what
// keeps the same shape language for a blink. A shut eye is a bar whatever the
// aperture says, because a blink does not travel through the ease.
static inline int buddyEyeHalfH(float ry, float grow, float open, bool shut) {
  if (shut) return 4;
  if (!(open > 0.0f)) return 4;
  if (open > 1.0f) open = 1.0f;
  float full = ry * grow;
  if (full < 4.0f) full = 4.0f;
  return (int)lroundf(4.0f + (full - 4.0f) * open);
}

// The face's energy as a whole percent of the owner's eye colour, so the eye and
// its three halos fade together: 0 turns the panel black, 100 is the shipped
// look, and everything between is the face coming up.
static inline int buddyGlowPercent(float glow) {
  if (!(glow > 0.0f)) return 0;
  if (glow >= 1.0f) return 100;
  return (int)lroundf(glow * 100.0f);
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
// Per-channel time constants. See the note above BUDDY_EASE_TAU_MS: small tau
// leads, large tau trails, and the gaze has to stay the fastest or the face
// stops looking like it is looking at anything.
#define BUDDY_EASE_TAU_GAZE_MS 45.0f   // the saccade: leads everything
#define BUDDY_EASE_TAU_SIZE_MS 90.0f   // grow / ry: settles in the middle
#define BUDDY_EASE_TAU_LID_MS 150.0f   // eyelids: heavy, always lagging

struct BuddyPoseFilter {
  bool seeded = false;
  float gazeX, gazeY, eyeGazeL, eyeGazeR;
  float rx, ry;
  float lid, lidL, lidR, bottom;
  float rot, rotL, rotR, hot, bob;
  float grow, tremble;
  float tilt, turn;
};

static inline void buddyPoseFilterReset(BuddyPoseFilter *f) {
  *f = BuddyPoseFilter();
}

// Writes the lid channel without touching the rest of the pose. Waking up uses it
// to start from a shut lid (the eye was a bar, which is a different shape, so the
// lid has no history to inherit) and then let the ease open it.
static inline void buddyPoseFilterSeedLid(BuddyPoseFilter *f, float lid) {
  if (!f) return;
  f->lid = lid;
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
    f->grow = target.grow;
    f->tremble = target.tremble;
    f->tilt = target.tilt;
    f->turn = target.turn;
    return target;
  }

  BuddyPose p = target;
  float tau = BUDDY_EASE_TAU_MS;
  f->gazeX = buddyEase(f->gazeX, target.gazeX, dtMs, BUDDY_EASE_TAU_GAZE_MS);
  f->gazeY = buddyEase(f->gazeY, target.gazeY, dtMs, BUDDY_EASE_TAU_GAZE_MS);
  f->eyeGazeL = buddyEase(f->eyeGazeL, target.eyeGazeL, dtMs, BUDDY_EASE_TAU_GAZE_MS);
  f->eyeGazeR = buddyEase(f->eyeGazeR, target.eyeGazeR, dtMs, BUDDY_EASE_TAU_GAZE_MS);
  f->rx = buddyEase(f->rx, (float)target.rx, dtMs, BUDDY_EASE_TAU_SIZE_MS);
  f->ry = buddyEase(f->ry, (float)target.ry, dtMs, BUDDY_EASE_TAU_SIZE_MS);
  f->lid = buddyEase(f->lid, target.lid, dtMs, BUDDY_EASE_TAU_LID_MS);
  f->lidL = buddyEase(f->lidL, target.lidL, dtMs, BUDDY_EASE_TAU_LID_MS);
  f->lidR = buddyEase(f->lidR, target.lidR, dtMs, BUDDY_EASE_TAU_LID_MS);
  f->bottom = buddyEase(f->bottom, target.bottom, dtMs, BUDDY_EASE_TAU_LID_MS);
  f->rot = buddyEase(f->rot, target.rot, dtMs, tau);
  f->rotL = buddyEase(f->rotL, target.rotL, dtMs, tau);
  f->rotR = buddyEase(f->rotR, target.rotR, dtMs, tau);
  f->hot = buddyEase(f->hot, target.hot, dtMs, tau);
  f->bob = buddyEase(f->bob, target.bob, dtMs, tau);
  f->grow = buddyEase(f->grow, target.grow, dtMs, BUDDY_EASE_TAU_SIZE_MS);
  f->tremble = buddyEase(f->tremble, target.tremble, dtMs, tau);
  f->tilt = buddyEase(f->tilt, target.tilt, dtMs, tau);
  f->turn = buddyEase(f->turn, target.turn, dtMs, tau);

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
  p.grow = f->grow;
  p.tremble = f->tremble;
  p.tilt = f->tilt;
  p.turn = f->turn;
  return p;
}
