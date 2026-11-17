#include "rotation.h"

#include "display.h"
#include "imu.h"
#include "screens.h"
#include "settings.h"

// ~10 Hz. The policy wants a settled reading, not every pass of the loop, and the
// I2C bus is shared with the game and with Sofia's face.
#define ROT_SAMPLE_MS 100

// On this board the glass is 320x172 landscape and the QMI8658 sits flat behind
// it: X carries the long axis, Y the short one, and Z looks out of the glass
// (buddy_imu.h proves the Z direction with its face-down test).
#define ROT_LONG_AXIS 0
#define ROT_SHORT_AXIS 1

static uint8_t s_mode = ROT_MODE_AUTO;
static bool s_locked = false;
static bool s_held = false;
static RotationPolicy s_policy;
static uint32_t s_lastSample = 0;

// Puts the image on the glass and, once the screens exist, repaints through the
// normal path so each screen's own enter hook runs exactly as it did for the
// console command that used to own this.
static void applyOrientation(uint8_t orientation) {
  if (orientation > 3) return;
  if (orientation == displayGetOrientation()) return;
  displaySetOrientation(orientation);
  settingsSetOrientation(orientation);
  screensGoTo(screensCurrent());
}

// The orientation the mode asks for, ignoring the sensor. Raw pins (0..3, still
// reachable from the console) and the two owner-facing values both land here.
static uint8_t pinnedOrientation(uint8_t mode) {
  return mode > 3 ? ORIENT_DEFAULT : mode;
}

void rotationBegin() {
  s_mode = settingsGetRotationMode();
  s_locked = settingsGetRotationLocked();
  s_held = false;

  uint8_t stored = settingsGetOrientation();
  // Automatic mode only ever shows one of the two unmirrored images, so a stored
  // mirrored value (a debug leftover, never something the owner wants) is folded
  // onto the real pair. A pinned mode keeps exactly what was stored.
  uint8_t seed = rotPolicySeed(stored);
  s_policy.applied = seed;
  s_policy.anchor = seed;
  s_policy.reference = 0;
  s_policy.haveRef = false;
  s_policy.candidate = 0;
  s_policy.sinceMs = 0;

  displaySetOrientation(rotModeIsConcrete(s_mode) ? pinnedOrientation(s_mode)
                                                  : seed);
}

void rotationTick(uint32_t nowMs) {
  // The first tick only arms the throttle, so boot does not sample immediately
  // after the I2C bus came up.
  if (s_lastSample == 0) {
    s_lastSample = nowMs;
    return;
  }
  if ((uint32_t)(nowMs - s_lastSample) < ROT_SAMPLE_MS) return;
  s_lastSample = nowMs;

  if (rotModeIsConcrete(s_mode)) {
    applyOrientation(pinnedOrientation(s_mode));
    return;
  }
  if (s_locked || s_held || !g_imuOk) return;
  // Nobody can see a rotation while the glass is off, and turning the gadget in a
  // drawer is not a request to wake it: keep the policy quiet until it is on.
  if (!displayIsAwake()) return;

  float accel[3], gyro[3];
  imuReadAll(accel, gyro);
  applyOrientation(
      rotPolicyFeed(&s_policy, accel, ROT_LONG_AXIS, ROT_SHORT_AXIS, nowMs));
}

uint8_t rotationMode() { return s_mode; }

const char *rotationModeName() {
  switch (s_mode) {
    case ROT_MODE_AUTO: return "auto";
    case ROT_MODE_CW: return "horizontal";
    case ROT_MODE_CCW: return "inverted";
    default: return "raw";
  }
}

bool rotationLocked() { return s_locked; }

uint8_t rotationApplied() { return displayGetOrientation(); }

int8_t rotationInPlaneSign() {
  // Nothing is known until one usable sample has taught which physical direction
  // means which image; until then the readings are used as they come.
  if (!s_policy.haveRef) return 1;
  return s_policy.applied == s_policy.anchor ? 1 : -1;
}

bool rotationSetMode(uint8_t mode) {
  if (!rotModeValid(mode)) return false;
  s_mode = mode;
  settingsSetRotationMode(mode);
  if (rotModeIsConcrete(mode)) {
    applyOrientation(pinnedOrientation(mode));
  } else if (!s_locked) {
    // Back to automatic: adopt the image already on the glass so the switch does
    // not jump, then let the sensor take over from here.
    s_policy.applied = rotPolicySeed(displayGetOrientation());
    s_policy.anchor = s_policy.applied;
    s_policy.haveRef = false;
    s_policy.candidate = 0;
    s_policy.sinceMs = 0;
  }
  return true;
}

bool rotationSetLocked(bool locked) {
  s_locked = locked;
  settingsSetRotationLocked(locked);
  if (locked) {
    // Locking freezes what is on the glass right now, so unlocking later cannot
    // surprise the owner with a jump to a decision taken minutes ago.
    s_policy.candidate = 0;
    settingsSetOrientation(displayGetOrientation());
  }
  return true;
}

void rotationSetHeld(bool held) {
  if (s_held == held) return;
  s_held = held;
  // The player has been tilting the board to steer: any candidate gathered while
  // held is a steering gesture, not a turn. Start clean when the hold lifts.
  s_policy.candidate = 0;
}

bool rotationIsHeld() { return s_held; }
