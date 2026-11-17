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

// Gravity has to lean at least this far along the long axis before the reading
// can mean "turned around" instead of "lying flat". In g.
#define ROT_MIN_G 0.35f
// ...and the long axis has to carry this much more gravity than the short one, so
// the roll a player uses to steer the dodge game never reads as a 180 degree
// turn: that gesture moves gravity onto the short axis, not past the long one.
#define ROT_DOMINANCE 1.5f
// A new decision has to hold this long before the image flips. Handling the
// gadget swings it about, and a wobble must not repaint the screen twice.
#define ROT_SETTLE_MS 450

// Owner-facing modes. The auto mode is deliberately not one of the driver's
// rotation numbers so a stored rotation can never be confused with it.
#define ROT_MODE_AUTO 4
#define ROT_MODE_CW 1
#define ROT_MODE_CCW 2

struct RotationPolicy {
  uint8_t applied;   // orientation on the panel: ROT_POLICY_A or ROT_POLICY_B
  uint8_t anchor;    // orientation the reference sign was observed with
  int8_t reference;  // sign of the long axis seen while `anchor` was applied
  bool haveRef;      // false until one usable sample has been seen
  int8_t candidate;  // orientation the recent samples point at, 0 when none
  uint32_t sinceMs;  // when that candidate first appeared
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

// Feeds one sample and returns the orientation to apply. `longAxis`/`shortAxis`
// are the two axes in the plane of the glass; on this board X (0) is the long one
// and Z (2) looks out of the glass, which is fixed by the layout, but they are
// parameters so the rule stays checkable on the host.
static inline uint8_t rotPolicyFeed(RotationPolicy *p, const float accel[3],
                                    uint8_t longAxis, uint8_t shortAxis,
                                    uint32_t nowMs) {
  if (!p) return ROT_POLICY_A;
  // A stored value that is mirrored or corrupt never reaches the glass: the
  // policy's own answers are the only ones it can produce.
  if (p->applied != ROT_POLICY_A && p->applied != ROT_POLICY_B) {
    p->applied = rotPolicySeed(p->applied);
  }
  int8_t applied = (int8_t)p->applied;
  if (!accel || longAxis > 2 || shortAxis > 2 || longAxis == shortAxis) {
    return p->applied;
  }

  float along = accel[longAxis];
  float across = accel[shortAxis];
  float magAlong = fabsf(along);
  float magAcross = fabsf(across);

  // Flat on its back, or a roll about the long axis: no decision this sample.
  // Dropping the candidate is what makes a wobble harmless.
  if (magAlong < ROT_MIN_G || magAlong < ROT_DOMINANCE * magAcross) {
    p->candidate = 0;
    return p->applied;
  }

  int8_t sign = along > 0.0f ? 1 : -1;

  // First usable sample: learn which sign means which image, from the image the
  // panel is actually showing right now. No hardware assumption has to be
  // written down, and it survives a reboot because the applied orientation is
  // the one that was stored.
  if (!p->haveRef) {
    p->haveRef = true;
    p->anchor = p->applied;
    p->reference = sign;
    p->candidate = 0;
    return p->applied;
  }

  int8_t want = (int8_t)(sign == p->reference ? p->anchor
                                              : rotPolicyOther(p->anchor));
  if (want == applied) {
    p->candidate = 0;
    return p->applied;
  }

  if (p->candidate != want) {
    p->candidate = want;
    p->sinceMs = nowMs;
    return p->applied;
  }
  if ((uint32_t)(nowMs - p->sinceMs) < ROT_SETTLE_MS) return p->applied;

  p->applied = (uint8_t)want;
  p->candidate = 0;
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
