// The idle ball, pure and header-only.
//
// Two quick taps on Sofía's screen put a red ball on the glass. The owner shakes
// the board, the ball bounces off the four screen edges, and the eyes follow it.
// The physics live here, away from Arduino and the framebuffer, so
// `tools/buddy_ball_test.cpp` can pin them without a board.
//
// The shake input is the *dynamic* part of the lateral accelerometer (raw minus
// its own slow tilt baseline), which the caller already filters for the gaze:
// a board held at a steady angle does not drag the ball into a corner, and only
// a real shake injects velocity. Screen x mirrors the IMU x (the same convention
// the gravity gaze uses), screen y matches the IMU y.
#pragma once

#include <math.h>
#include <stdint.h>

// The playfield is the whole glass: the drawings elsewhere leave the eye line in
// the middle, but the ball is allowed to cover it (the eyes follow it).
#define BUDDY_BALL_SCREEN_W 320.0f
#define BUDDY_BALL_SCREEN_H 172.0f
#define BUDDY_BALL_RADIUS 13.0f
// How much a steady tilt (in g of screen-space gravity) accelerates the ball.
// 1 g is the board held vertical; at 520 the ball crosses the glass in about a
// second and clearly falls towards the low edge.
#define BUDDY_BALL_GRAVITY_GAIN 520.0f
// How much dynamic lateral acceleration (in g) becomes an extra kick. 2 g of
// shake is a deliberate flick, so 520 px/s^2 per g reads as a firm push.
#define BUDDY_BALL_SHAKE_GAIN 520.0f
#define BUDDY_BALL_MAX_SPEED 800.0f
// Each wall hit keeps this fraction of the incoming speed: this is a bouncy
// ball, not a beanbag, so a lively hit stays lively through several walls.
#define BUDDY_BALL_RESTITUTION 0.92f
// Exponential speed decay, 1/s. Almost no drag: gravity, not friction, decides
// where the ball ends up.
#define BUDDY_BALL_FRICTION 0.25f
// A ball pressed into a wall by gravity bounces every frame if every hit flips
// the velocity. Below this speed the hit is absorbed and the ball rests.
#define BUDDY_BALL_REST_SPEED 60.0f
// A stalled frame must not integrate half a second of shake in one jump.
#define BUDDY_BALL_MAX_DT_MS 100u

// How far the gaze can leave the eye line while chasing the ball. The ball can
// reach the glass edges; the eyes come back before they run out of face.
#define BUDDY_BALL_GAZE_X 24.0f
#define BUDDY_BALL_GAZE_Y 16.0f

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
// spawns do not look like a static sprite. Same seed, same nudge.
static inline void buddyBallSpawn(BuddyBall *b, uint32_t seed) {
  if (!b) return;
  *b = BuddyBall();
  b->active = true;
  float nx = (float)((seed >> 8) & 0xFFu) / 127.5f - 1.0f;
  float ny = (float)((seed >> 16) & 0xFFu) / 127.5f - 1.0f;
  b->vx = nx * 60.0f;
  b->vy = ny * 45.0f;
}

// One physics step. `gravityX`/`gravityY` are the steady tilt in screen space
// (g, positive = right/down, already passed through the steering calibration)
// and `shakeX`/`shakeY` the dynamic part above that tilt. Zero gravity means the
// board is level; zero shake means the owner is holding it still.
static inline void buddyBallStep(BuddyBall *b, float gravityX, float gravityY,
                                 float shakeX, float shakeY, uint32_t dtMs) {
  if (!b || !b->active || dtMs == 0) return;
  if (dtMs > BUDDY_BALL_MAX_DT_MS) dtMs = BUDDY_BALL_MAX_DT_MS;
  float dt = (float)dtMs / 1000.0f;

  // The world pulls the ball down the slope, and a shake adds a kick on top.
  // Screen x mirrors the IMU x (the same convention the gaze uses).
  b->vx += (gravityX * BUDDY_BALL_GRAVITY_GAIN +
            shakeX * BUDDY_BALL_SHAKE_GAIN) * dt;
  b->vy += (gravityY * BUDDY_BALL_GRAVITY_GAIN +
            shakeY * BUDDY_BALL_SHAKE_GAIN) * dt;

  float damp = 1.0f / (1.0f + BUDDY_BALL_FRICTION * dt);
  b->vx *= damp;
  b->vy *= damp;

  float speed = sqrtf(b->vx * b->vx + b->vy * b->vy);
  if (speed > BUDDY_BALL_MAX_SPEED) {
    float scale = BUDDY_BALL_MAX_SPEED / speed;
    b->vx *= scale;
    b->vy *= scale;
  }

  b->x += b->vx * dt;
  b->y += b->vy * dt;

  const float lo = BUDDY_BALL_RADIUS;
  const float hiX = BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS;
  const float hiY = BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS;
  if (b->x < lo) {
    b->x = lo;
    if (b->vx < 0.0f) {
      b->vx = -b->vx >= BUDDY_BALL_REST_SPEED
                  ? -b->vx * BUDDY_BALL_RESTITUTION
                  : 0.0f;
    }
  } else if (b->x > hiX) {
    b->x = hiX;
    if (b->vx > 0.0f) {
      b->vx = b->vx >= BUDDY_BALL_REST_SPEED
                  ? -b->vx * BUDDY_BALL_RESTITUTION
                  : 0.0f;
    }
  }
  if (b->y < lo) {
    b->y = lo;
    if (b->vy < 0.0f) {
      b->vy = -b->vy >= BUDDY_BALL_REST_SPEED
                  ? -b->vy * BUDDY_BALL_RESTITUTION
                  : 0.0f;
    }
  } else if (b->y > hiY) {
    b->y = hiY;
    if (b->vy > 0.0f) {
      b->vy = b->vy >= BUDDY_BALL_REST_SPEED
                  ? -b->vy * BUDDY_BALL_RESTITUTION
                  : 0.0f;
    }
  }
}

// Where the eyes should look to follow the ball. The eye line sits at y=88 and
// the pair is centred at x=160; the result is a bounded offset from that line,
// so a ball at a corner leans the pair without pulling the eyes off the glass.
static inline void buddyBallGaze(const BuddyBall &b, float *gazeX,
                                 float *gazeY) {
  float gx = (b.x - BUDDY_BALL_SCREEN_W * 0.5f) / 7.0f;
  float gy = (b.y - 88.0f) / 8.0f;
  if (gazeX) *gazeX = buddyBallClamp(gx, -BUDDY_BALL_GAZE_X, BUDDY_BALL_GAZE_X);
  if (gazeY) *gazeY = buddyBallClamp(gy, -BUDDY_BALL_GAZE_Y, BUDDY_BALL_GAZE_Y);
}
