// The idle ball, pure and header-only, in real units.
//
// Two quick taps on Sofía's screen put a red ball on the glass. The owner tilts
// and shakes the board, the ball bounces off the four screen edges, and the eyes
// follow it. The physics live here, away from Arduino and the framebuffer, so
// `tools/buddy_ball_test.cpp` can pin them without a board.
//
// Why this file uses SI units: the owner said the ball "sigue siendo medio
// liviana" and asked for the gravity of planet Earth. The old model was a gain
// in px/s^2 per unit of tilt (520), which is ~184x weaker than the real thing.
// Here every number is a physical quantity with a unit, converted to pixels
// exactly once:
//
//   standard gravity   g0   = 9.80665 m/s^2   (3rd CGPM, 1901 — the conventional
//                                             value every gravity table quotes)
//   the glass          172 x 320 px, 1.47 in diagonal of active area
//                      -> 17.68 x 32.89 mm -> 9728 px per metre
//   so 1 g of tilt    = 9.80665 * 9728 = 95,401 px/s^2
//
// A 32.9 mm free fall at that acceleration lasts 82 ms, which is what a real ball
// would do on a strip this small and is impossible to watch. The simulation
// therefore runs on a slowed clock: BUDDY_BALL_SLOWMO wall seconds per simulated
// second, applied to the clock and not to the acceleration, so the trajectory is
// exactly the physical one, played back slowly. Everything derived from gravity
// (transit time, bounce height, terminal speed) keeps its physical ratio; only
// the playback is slowed, and the slowdown is a single documented number.
//
// Restitution is the real coefficient for rubber on glass (0.8-0.9); stiffer than
// that reads as a superball. Air drag on a 2.7 mm ball at 0.8 m/s is under 5% of
// its weight, so it is not modelled: gravity and restitution are the whole model.
//
// The shake input is the *dynamic* part of the lateral accelerometer (raw minus
// its own slow tilt baseline), which the caller already filters for the gaze: a
// board held at a steady angle does not drag the ball into a corner, and only a
// real shake injects acceleration. Screen x mirrors the IMU x (the same
// convention the gravity gaze uses), screen y matches the IMU y. Both inputs are
// in g, the unit imu.h reports.
#pragma once

#include <math.h>
#include <stdint.h>

// The playfield is the whole glass: the drawings elsewhere leave the eye line in
// the middle, but the ball is allowed to cover it (the eyes follow it).
#define BUDDY_BALL_SCREEN_W 320.0f
#define BUDDY_BALL_SCREEN_H 172.0f
#define BUDDY_BALL_RADIUS 13.0f  // 2.67 mm across, a marble on this panel

// --- the real world, converted once -----------------------------------------
// Standard gravity, exact by definition since 1901.
#define BUDDY_BALL_G 9.80665f
// Pixels per metre: 320 px over 32.89 mm of active height (1.47 in diagonal with
// a 172:320 panel, so h = 1.47 in / sqrt(1 + (172/320)^2) = 32.89 mm).
#define BUDDY_BALL_PX_PER_M 9728.0f
// Simulated seconds per wall second. 0.15 turns the 82 ms real fall into 545 ms,
// which is watchable. This is a playback factor, not a physics parameter.
#define BUDDY_BALL_SLOWMO 0.15f
// Rubber on glass.
#define BUDDY_BALL_RESTITUTION 0.90f
// Below 2 cm/s a bounce is absorbed instead of flipped: a ball pressed into the
// low edge by gravity rests there instead of chattering every frame.
#define BUDDY_BALL_REST_SPEED 195.0f
// Safety rail, not a game parameter: sqrt(2*g*h) for a full-height fall is
// 5730 px/s, so nothing physical reaches this. It only keeps a shake spike from
// launching the ball through a wall between two substeps.
#define BUDDY_BALL_MAX_SPEED 6000.0f
// Semi-implicit integration is stable and exact with small steps; 1 ms of
// *simulated* time keeps the error under a pixel and cannot tunnel: at the speed
// cap a substep moves 6 px, well inside the 13 px radius.
#define BUDDY_BALL_SUBSTEP_MS 1.0f
// A stalled frame must not integrate half a second of shake in one jump.
#define BUDDY_BALL_MAX_DT_MS 100u

// How far the gaze can leave the eye line while chasing the ball. The ball can
// reach the glass edges; the eyes come back before they run out of face.
#define BUDDY_BALL_GAZE_X 24.0f
#define BUDDY_BALL_GAZE_Y 16.0f

// The one derived constant: a g of tilt, in pixels per second squared.
#define BUDDY_BALL_ACCEL (BUDDY_BALL_G * BUDDY_BALL_PX_PER_M)

struct BuddyBall {
  bool active = false;
  float x = BUDDY_BALL_SCREEN_W * 0.5f;
  float y = BUDDY_BALL_SCREEN_H * 0.5f;
  float vx = 0;
  float vy = 0;
};

static inline float buddyBallClamp(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline void buddyBallClear(BuddyBall *b) {
  if (!b) return;
  b->active = false;
  b->vx = 0;
  b->vy = 0;
}

// Puts the ball on the glass, centred, with a small deterministic drift so two
// spawns do not look like a static sprite. Same seed, same nudge. The drift is
// 1 cm/s: under real gravity the first frame decides where it goes anyway.
static inline void buddyBallSpawn(BuddyBall *b, uint32_t seed) {
  if (!b) return;
  *b = BuddyBall();
  b->active = true;
  float nx = (float)((seed >> 8) & 0xFFu) / 127.5f - 1.0f;
  float ny = (float)((seed >> 16) & 0xFFu) / 127.5f - 1.0f;
  b->vx = nx * 97.0f;
  b->vy = ny * 73.0f;
}

// One edge, shared by all four walls: clamp, then either bounce with the real
// restitution or absorb the hit when the ball is too slow to be bouncing.
static inline void buddyBallWall(float *pos, float *vel, float lo, float hi) {
  if (*pos < lo) {
    *pos = lo;
    if (fabsf(*vel) > BUDDY_BALL_REST_SPEED) {
      *vel = -(*vel) * BUDDY_BALL_RESTITUTION;
    } else {
      *vel = 0;
    }
  } else if (*pos > hi) {
    *pos = hi;
    if (fabsf(*vel) > BUDDY_BALL_REST_SPEED) {
      *vel = -(*vel) * BUDDY_BALL_RESTITUTION;
    } else {
      *vel = 0;
    }
  }
}

// Advances the ball by `dtMs` of wall time. Tilt and shake arrive in g, added
// because both are accelerations of the frame the ball sits in: a shake is the
// same physical thing as a tilt, only faster.
static inline void buddyBallStep(BuddyBall *b, float tiltX, float tiltY,
                                 float shakeX, float shakeY, uint32_t dtMs) {
  if (!b || !b->active) return;
  if (dtMs == 0) return;
  if (dtMs > BUDDY_BALL_MAX_DT_MS) dtMs = BUDDY_BALL_MAX_DT_MS;

  const float simMs = (float)dtMs * BUDDY_BALL_SLOWMO;
  int steps = (int)(simMs / BUDDY_BALL_SUBSTEP_MS) + 1;
  const float h = simMs / (float)steps / 1000.0f;  // seconds per substep

  const float ax = BUDDY_BALL_ACCEL * (tiltX + shakeX);
  const float ay = BUDDY_BALL_ACCEL * (tiltY + shakeY);
  const float lo = BUDDY_BALL_RADIUS;
  const float hiX = BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS;
  const float hiY = BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS;

  for (int i = 0; i < steps; i++) {
    // Velocity Verlet: with a constant acceleration this is exact, which is what
    // lets the host test compare the fall against y = 1/2 g t^2 instead of
    // tolerating an integrator's drift.
    b->x += b->vx * h + 0.5f * ax * h * h;
    b->y += b->vy * h + 0.5f * ay * h * h;
    b->vx += ax * h;
    b->vy += ay * h;

    const float speed = sqrtf(b->vx * b->vx + b->vy * b->vy);
    if (speed > BUDDY_BALL_MAX_SPEED && speed > 0.0f) {
      const float k = BUDDY_BALL_MAX_SPEED / speed;
      b->vx *= k;
      b->vy *= k;
    }

    buddyBallWall(&b->x, &b->vx, lo, hiX);
    buddyBallWall(&b->y, &b->vy, lo, hiY);
  }
}

// Where the eyes should look to follow the ball, in pixels of gaze offset, kept
// inside the face's reach.
static inline void buddyBallGaze(const BuddyBall &b, float *gazeX, float *gazeY) {
  float gx = (b.x - BUDDY_BALL_SCREEN_W * 0.5f) / 7.0f;
  float gy = (b.y - BUDDY_BALL_SCREEN_H * 0.5f) / 5.0f;
  *gazeX = buddyBallClamp(gx, -BUDDY_BALL_GAZE_X, BUDDY_BALL_GAZE_X);
  *gazeY = buddyBallClamp(gy, -BUDDY_BALL_GAZE_Y, BUDDY_BALL_GAZE_Y);
}
