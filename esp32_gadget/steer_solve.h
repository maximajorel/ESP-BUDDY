// Guided calibration solver: from four tilt captures, name the axes and signs.
//
// The old calibration recorded one left/right gesture and guessed the rest. With
// two axes moving the ship that guess is not good enough: a wrist that rolls
// slightly makes the wrong axis look like the lateral one, and no gesture can
// teach the sign of "up". This solver takes four captures -- tilt toward you,
// away, left, right -- and derives the two in-plane axes, the gravity axis, both
// signs and both neutral baselines. Pure and header-only so
// `tools/steer_solve_test.cpp` pins the same code the firmware runs.
#pragma once

#include <math.h>
#include <stdint.h>

// A capture smaller than this is a hand tremor, not a gesture. In g.
#define STEER_MIN_DELTA_G 0.05f

struct SteerSolution {
  bool ok;
  uint8_t latAxis;    // in-plane axis that reads left/right
  uint8_t fwdAxis;    // in-plane axis that reads toward/away
  uint8_t upAxis;     // axis that carries gravity
  int8_t sign;        // +1: increasing latAxis moves the ship right
  int8_t signY;       // +1: increasing fwdAxis means tilting the top edge away
                      // (the game negates this when it turns it into movement)
  float baseline;     // neutral reading on latAxis
  float baselineY;    // neutral reading on fwdAxis
};

// `neutral` is the device at rest; the other four are the raw accelerometer
// readings at the extremes. `toward`/`away` tilt the top edge toward/away from
// the player; `left`/`right` tilt the left/right edge down.
static inline SteerSolution steerSolve(const float neutral[3],
                                       const float toward[3],
                                       const float away[3],
                                       const float left[3],
                                       const float right[3]) {
  SteerSolution out;
  out.ok = false;
  out.latAxis = 1;
  out.fwdAxis = 0;
  out.upAxis = 2;
  out.sign = 1;
  out.signY = 1;
  out.baseline = 0.0f;
  out.baselineY = 0.0f;
  if (!neutral || !toward || !away || !left || !right) return out;

  // Gravity is the axis with the largest resting reading, whatever the sign.
  uint8_t up = 0;
  for (uint8_t axis = 1; axis < 3; axis++) {
    if (fabsf(neutral[axis]) > fabsf(neutral[up])) up = axis;
  }

  // The other two axes are the plane the player tilts in. Which one reads
  // left/right is decided by the horizontal gesture, not by a guess: try both
  // assignments and keep the one whose two gestures respond more strongly.
  uint8_t plane[2];
  uint8_t found = 0;
  for (uint8_t axis = 0; axis < 3 && found < 2; axis++) {
    if (axis != up) plane[found++] = axis;
  }
  if (found != 2) return out;

  float dv[3];
  float dh[3];
  for (uint8_t axis = 0; axis < 3; axis++) {
    dv[axis] = away[axis] - toward[axis];
    dh[axis] = right[axis] - left[axis];
  }

  uint8_t lat = plane[0];
  uint8_t fwd = plane[1];
  float straight = fabsf(dh[plane[0]]) + fabsf(dv[plane[1]]);
  float swapped = fabsf(dh[plane[1]]) + fabsf(dv[plane[0]]);
  if (swapped > straight) {
    lat = plane[1];
    fwd = plane[0];
  }

  // Both gestures have to be real gestures on the axis that ended up owning
  // them, otherwise the previous calibration is a better answer than a guess.
  if (fabsf(dh[lat]) < STEER_MIN_DELTA_G) return out;
  if (fabsf(dv[fwd]) < STEER_MIN_DELTA_G) return out;
  if (lat == fwd || lat == up || fwd == up) return out;

  out.ok = true;
  out.latAxis = lat;
  out.fwdAxis = fwd;
  out.upAxis = up;
  out.sign = dh[lat] > 0.0f ? 1 : -1;
  out.signY = dv[fwd] > 0.0f ? 1 : -1;
  out.baseline = neutral[lat];
  out.baselineY = neutral[fwd];
  return out;
}
