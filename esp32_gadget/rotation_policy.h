// Which of the two landscape orientations the panel should show, decided from
// the accelerometer. Pure and header-only so `tools/rotation_policy_test.cpp`
// pins the same rules the firmware runs.
//
// The glass is a portrait strip driven in landscape (see display.h): of the four
// rotations the driver can blit, two are mirrored and two are the real thing, 180
// degrees apart. This policy only ever answers with those two.
//
// The honest limit: a board lying flat on its back has gravity along the axis
// that looks out of the glass and nothing in the plane, so no accelerometer can
// tell which way it is turned. The policy keeps the last image instead of
// guessing, and the owner has the lock and the two manual modes for everything
// else.
#pragma once

#include <math.h>
#include <stdint.h>

// The two unmirrored rotations, equal to ORIENT_CW and ORIENT_CCW in display.h.
// They are repeated here so this file stays free of Arduino headers and can be
// compiled on the host.
#define ROT_POLICY_A 1
#define ROT_POLICY_B 2

// Gravity has to lean at least this far into the plane of the glass before a
// reading can mean "turned around" instead of "lying flat". In g. Measured on
// the projection, whichever in-plane axis carries it: the owner reads the
// 320x172 image held landscape, where the long axis is horizontal and the short
// one takes the gravity, and also holds it upright, where the long axis does.
#define ROT_MIN_G 0.35f
// Cosine of the angle between the in-plane gravity now and the one seen when the
// current image was upright. The two images are 180 degrees apart, so only an
// outright reversal counts: -0.50 is 120 degrees away, which a steering roll or
// a forward tilt never reaches, and turning the board around overshoots.
#define ROT_FLIP_DOT (-0.50f)
// ...and the way back. The gap between the two is the hysteresis that keeps a
// board resting near the boundary from repainting every second.
#define ROT_BACK_DOT 0.50f
// A new decision has to hold this long before the image flips. Handling the
// gadget swings it about, and a wobble must not repaint the screen twice.
#define ROT_SETTLE_MS 450
// How long the same direction has to persist, with no decision being made, before
// it is taken as the new "upright" for the image on the glass. The owner changes
// grip, and the gadget is often switched on in whatever pose it was left in; a
// reference learned once would leave every other hold unable to ever report a
// turn. A real turn decides in ROT_SETTLE_MS, far shorter than this, and a very
// slow turn is indistinguishable from a grip change by gravity alone.
#define ROT_REANCHOR_MS 2000

// Owner-facing modes. The auto mode is deliberately not one of the driver's
// rotation numbers so a stored rotation can never be confused with it.
#define ROT_MODE_AUTO 4
#define ROT_MODE_CW 1
#define ROT_MODE_CCW 2

struct RotationPolicy {
  uint8_t applied;   // orientation on the panel: ROT_POLICY_A or ROT_POLICY_B
  uint8_t anchor;    // orientation the reference direction was observed with
  float refX;        // unit in-plane gravity seen while `anchor` was applied
  float refY;
  bool haveRef;      // false until one usable sample has been seen
  int8_t candidate;  // orientation the recent samples point at, 0 when none
  uint32_t sinceMs;  // when that candidate first appeared
  uint32_t stableSinceMs;  // since when the direction has been steady
};

// A stored rotation (0..3, ROT_MODE_AUTO or anything corrupt) folded onto the
// two unmirrored ones. Used to seed the policy at boot and to keep a corrupt or
// mirrored NVS value from ever reaching the panel.
static inline uint8_t rotPolicySeed(uint8_t stored) {
  return stored == ROT_POLICY_B ? ROT_POLICY_B : ROT_POLICY_A;
}

static inline uint8_t rotPolicyOther(uint8_t orientation) {
  return orientation == ROT_POLICY_A ? ROT_POLICY_B : ROT_POLICY_A;
}

// Seeds a policy that will decide from the image on the glass right now. Every
// place that adopts or adopts-again the automatic mode starts from here, so the
// reference is never half-carried over from a mode that had stopped listening to
// the sensor.
static inline void rotPolicyStart(RotationPolicy *p, uint8_t applied) {
  if (!p) return;
  p->applied = rotPolicySeed(applied);
  p->anchor = p->applied;
  p->refX = 0.0f;
  p->refY = 0.0f;
  p->haveRef = false;
  p->candidate = 0;
  p->sinceMs = 0;
  p->stableSinceMs = 0;
}

// Feeds one sample and returns the orientation to apply. `longAxis`/`shortAxis`
// are the two axes in the plane of the glass; on this board X (0) and Y (1) are
// the in-plane pair and Z (2) looks out of the glass, which is fixed by the
// layout, but they are parameters so the rule stays checkable on the host.
//
// The decision is made on the direction of the in-plane gravity, not on one
// axis: a board turned 180 degrees in its own plane reports the same vector
// negated, whichever of the two axes happens to carry it. A steering roll or a
// forward tilt moves the magnitude between axes and out of the plane but leaves
// the direction alone, so neither reads as a turn.
static inline uint8_t rotPolicyFeed(RotationPolicy *p, const float accel[3],
                                    uint8_t longAxis, uint8_t shortAxis,
                                    uint32_t nowMs) {
  if (!p) return ROT_POLICY_A;
  // A stored value that is mirrored or corrupt never reaches the glass: the
  // policy's own answers are the only ones it can produce.
  if (p->applied != ROT_POLICY_A && p->applied != ROT_POLICY_B) {
    p->applied = rotPolicySeed(p->applied);
  }
  if (!accel || longAxis > 2 || shortAxis > 2 || longAxis == shortAxis) {
    return p->applied;
  }

  float along = accel[longAxis];
  float across = accel[shortAxis];
  float mag = sqrtf(along * along + across * across);

  // Flat on its back or face down: nothing in the plane to look at. Keep the
  // last image instead of guessing, and let the sample abstain. It must not
  // discard a window another sample started: the board passes through exactly
  // this reading while it is being turned, and restarting the settle time on
  // every such sample is what made a real turn sit undecided forever.
  if (mag < ROT_MIN_G) return p->applied;

  float nx = along / mag;
  float ny = across / mag;

  // First usable sample: learn which direction means which image, from the image
  // the panel is actually showing right now. No hardware assumption has to be
  // written down, and it survives a reboot because the applied orientation is
  // the one that was stored.
  if (!p->haveRef) {
    p->haveRef = true;
    p->anchor = p->applied;
    p->refX = nx;
    p->refY = ny;
    p->candidate = 0;
    p->stableSinceMs = nowMs;
    return p->applied;
  }

  float dot = nx * p->refX + ny * p->refY;

  // Only a reversal of the in-plane gravity is a turn. The two thresholds leave
  // a wide band in the middle where the image simply stays as it is.
  uint8_t want = p->applied;
  if (p->applied == p->anchor) {
    if (dot <= ROT_FLIP_DOT) want = rotPolicyOther(p->anchor);
  } else {
    if (dot >= ROT_BACK_DOT) want = p->anchor;
  }

  // The direction this hold gives the anchored image: when the other image is on
  // the glass, the same physical hold points the other way.
  float anchorX = (p->applied == p->anchor) ? nx : -nx;
  float anchorY = (p->applied == p->anchor) ? ny : -ny;

  if (want == p->applied) {
    p->candidate = 0;
    p->sinceMs = 0;
    // No decision for a while: this is simply how the owner is holding it, so it
    // becomes the reference the next turn is measured against.
    if (p->stableSinceMs == 0) {
      p->stableSinceMs = nowMs;
    } else if ((uint32_t)(nowMs - p->stableSinceMs) >= ROT_REANCHOR_MS) {
      p->refX = anchorX;
      p->refY = anchorY;
      p->stableSinceMs = nowMs;
    }
    return p->applied;
  }

  // A decision is being confirmed: nothing about this hold is stable.
  p->stableSinceMs = 0;
  if (p->candidate != (int8_t)want) {
    p->candidate = (int8_t)want;
    p->sinceMs = nowMs;
  }
  if ((uint32_t)(nowMs - p->sinceMs) < ROT_SETTLE_MS) return p->applied;

  p->applied = want;
  p->candidate = 0;
  p->sinceMs = 0;
  p->stableSinceMs = 0;
  return p->applied;
}

// Points an accelerometer/gyroscope sample at the image the owner is looking at.
//
// Turning the gadget 180 degrees in its own plane reverses the two in-plane axes
// and leaves the glass axis alone, for both a vector (acceleration) and a
// pseudovector (angular velocity): a half turn is still a turn about the same
// normal, so the yaw reading is untouched. `sign` is +1 when the sample already
// matches the image, -1 when the image is the other one.
static inline void rotMirrorInPlane(float accel[3], float gyro[3], int8_t sign) {
  if (sign >= 0) return;
  if (accel) {
    accel[0] = -accel[0];
    accel[1] = -accel[1];
  }
  if (gyro) {
    gyro[0] = -gyro[0];
    gyro[1] = -gyro[1];
  }
}

// --- the mode the owner picks ------------------------------------------------

// Three values, no more: automatic, one image, the other image. A mirrored
// rotation is never offered because a mirrored image is never what an owner
// wants; it only ever appears when firmware is wrong.
static inline uint8_t rotModeCount() { return 3; }

// The next value in the cycle a tap walks: automatic -> horizontal -> inverted
// -> automatic. A raw rotation (the console can still pin the mirrored debug
// values) and anything corrupt read as automatic, so one tap from a store the
// screen does not understand lands back on the automatic mode.
static inline uint8_t rotModeCycle(uint8_t mode) {
  if (mode == ROT_MODE_CW) return ROT_MODE_CCW;
  if (mode == ROT_MODE_CCW) return ROT_MODE_AUTO;
  return mode == ROT_MODE_AUTO ? ROT_MODE_CW : ROT_MODE_AUTO;
}

// 0 for automatic, 1 for a mode that pins one image. A stored value outside
// the five the firmware writes reads as automatic, so a corrupt store can never
// freeze the image on a rotation nobody chose.
static inline uint8_t rotModeValid(uint8_t mode) {
  return mode <= ROT_MODE_AUTO ? 1 : 0;
}

static inline uint8_t rotModeIsConcrete(uint8_t mode) {
  return (rotModeValid(mode) && mode != ROT_MODE_AUTO) ? 1 : 0;
}
