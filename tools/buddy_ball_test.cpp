// Host-side test for the idle ball (S2, S3) in real units.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_ball_test.cpp -o /tmp/bbt && /tmp/bbt
//
// The old model was a gain of 520 px/s^2 per unit of tilt, ~184x weaker than
// gravity on a 1.47" panel, which is why the owner called the ball "liviana".
// This test pins the physical model instead of a feel: a free fall matches
// y = 1/2 g t^2 on the slowed clock, a level board neither accelerates nor drags,
// a wall returns the real coefficient of restitution, nothing tunnels and a slow
// ball rests instead of chattering.

#include <cstdio>
#include <math.h>

#include "buddy_ball.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static bool near(float a, float b, float tol) { return fabsf(a - b) <= tol; }

// The ball in the middle, at rest: tests below add their own velocity.
static BuddyBall restBall() {
  BuddyBall b;
  buddyBallSpawn(&b, 1234);
  b.vx = 0;
  b.vy = 0;
  return b;
}

int main() {
  printf("-- the constants have units\n");
  {
    check("standard gravity is the 1901 convention", near(BUDDY_BALL_G, 9.80665f, 0.0f));
    check("the glass is 9728 px per metre", near(BUDDY_BALL_PX_PER_M, 9728.0f, 0.5f));
    // 320 px of height is 32.89 mm, so a metre is 320/0.03289 px.
    check("the scale matches the panel", near(320.0f / 0.03289f, BUDDY_BALL_PX_PER_M, 3.0f));
    check("one g of tilt is ~95k px/s^2",
          near(BUDDY_BALL_ACCEL, 95401.0f, 60.0f));
    check("the playback is slowed", BUDDY_BALL_SLOWMO > 0.0f && BUDDY_BALL_SLOWMO < 1.0f);
    check("rubber on glass sits in the real band",
          BUDDY_BALL_RESTITUTION >= 0.80f && BUDDY_BALL_RESTITUTION <= 0.90f);
    check("the speed rail is above a full-height fall",
          BUDDY_BALL_MAX_SPEED > sqrtf(2.0f * BUDDY_BALL_ACCEL * BUDDY_BALL_SCREEN_H));
  }

  printf("-- a free fall is 1/2 g t^2\n");
  {
    BuddyBall b = restBall();
    b.y = 40.0f;
    // 200 ms of wall time is 30 ms of simulated time.
    for (int i = 0; i < 20; i++) buddyBallStep(&b, 0.0f, 1.0f, 0.0f, 0.0f, 10);
    const float tSim = 0.200f * BUDDY_BALL_SLOWMO;
    const float expected = 0.5f * BUDDY_BALL_ACCEL * tSim * tSim;
    const float fell = b.y - 40.0f;
    check("it falls the distance the formula says", near(fell, expected, expected * 0.005f));
    check("the velocity is g t", near(b.vy, BUDDY_BALL_ACCEL * tSim, 2.0f));
    // The same 200 ms of real time at full speed would cross the whole glass,
    // which is exactly why the playback factor exists.
    const float unslowed = 0.5f * BUDDY_BALL_ACCEL * 0.2f * 0.2f;
    check("without the slowdown it would cross the glass", unslowed > BUDDY_BALL_SCREEN_H);
  }

  printf("-- a level board neither pushes nor drags\n");
  {
    BuddyBall b = restBall();
    b.vx = 2000.0f;
    b.vy = 0.0f;
    const float x0 = b.x;
    buddyBallStep(&b, 0.0f, 0.0f, 0.0f, 0.0f, 100);
    check("the horizontal speed survives", near(b.vx, 2000.0f, 1.0f));
    check("the vertical speed stays zero", near(b.vy, 0.0f, 0.001f));
    // 15 ms of simulated time at 2000 px/s is 30 px.
    check("it travels v t", near(b.x - x0, 30.0f, 0.5f));
  }

  printf("-- a shake is an acceleration, not a kick\n");
  {
    BuddyBall b = restBall();
    buddyBallStep(&b, 0.0f, 0.0f, 2.0f, 0.0f, 100);
    check("a 2 g shake from rest builds speed", b.vx > 500.0f);
    BuddyBall still = restBall();
    buddyBallStep(&still, 0.0f, 0.0f, 0.0f, 0.0f, 100);
    check("no acceleration, no push", near(still.vx, 0.0f, 0.001f));
  }

  printf("-- walls eat the real restitution\n");
  {
    BuddyBall b = restBall();
    b.x = BUDDY_BALL_SCREEN_W * 0.5f;
    b.y = BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS - 1.0f;
    b.vx = 0.0f;
    b.vy = 3000.0f;
    buddyBallStep(&b, 0.0f, 0.0f, 0.0f, 0.0f, 5);
    check("the ball does not pass the floor",
          b.y <= BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS + 0.01f);
    check("it comes back", b.vy < 0.0f);
    check("it keeps the coefficient",
          near(fabsf(b.vy), 3000.0f * BUDDY_BALL_RESTITUTION, 20.0f));
  }

  printf("-- nothing tunnels\n");
  {
    BuddyBall b = restBall();
    b.x = BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS - 2.0f;
    b.y = BUDDY_BALL_SCREEN_H * 0.5f;
    b.vx = 50.0f * BUDDY_BALL_MAX_SPEED;
    b.vy = 0.0f;
    buddyBallStep(&b, 0.0f, 0.0f, 0.0f, 0.0f, 100);
    check("a frantic ball stays on the glass",
          b.x >= BUDDY_BALL_RADIUS - 0.01f &&
              b.x <= BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS + 0.01f);
    check("the cap keeps it finite", fabsf(b.vx) <= BUDDY_BALL_MAX_SPEED + 1.0f);
  }

  printf("-- a slow ball rests\n");
  {
    BuddyBall b = restBall();
    b.x = BUDDY_BALL_SCREEN_W * 0.5f;
    b.y = BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS;
    b.vx = 0.0f;
    b.vy = 50.0f;  // under the rest speed, no tilt
    buddyBallStep(&b, 0.0f, 0.0f, 0.0f, 0.0f, 20);
    check("the hit is absorbed", near(b.vy, 0.0f, 0.001f));
    check("the ball sits on the floor",
          near(b.y, BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS, 0.01f));
  }

  printf("-- the field\n");
  {
    BuddyBall b;
    buddyBallSpawn(&b, 1234);
    check("a spawn makes the ball active", b.active);
    check("it starts on the glass",
          b.x >= BUDDY_BALL_RADIUS && b.x <= BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS &&
              b.y >= BUDDY_BALL_RADIUS &&
              b.y <= BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS);
    check("it starts near the middle",
          fabsf(b.x - BUDDY_BALL_SCREEN_W * 0.5f) < 1.0f &&
              fabsf(b.y - BUDDY_BALL_SCREEN_H * 0.5f) < 1.0f);
    BuddyBall c;
    buddyBallSpawn(&c, 1234);
    check("the spawn is deterministic", b.vx == c.vx && b.vy == c.vy);
    buddyBallClear(&b);
    check("clearing turns it off", !b.active);
    check("a cleared ball does not move", near(b.vx, 0.0f, 0.0f));
  }

  printf("-- it never escapes a shake\n");
  {
    BuddyBall b;
    buddyBallSpawn(&b, 99);
    for (int i = 0; i < 200; i++) {
      // Alternating 3 g shake in both axes: the worst case a hand can produce.
      const float s = (i % 2) ? 3.0f : -3.0f;
      buddyBallStep(&b, s * 0.5f, -s * 0.5f, s, -s, 50);
      if (b.x < BUDDY_BALL_RADIUS - 0.01f || b.x > BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS + 0.01f ||
          b.y < BUDDY_BALL_RADIUS - 0.01f || b.y > BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS + 0.01f) {
        printf("     escaped at step %d: %.2f %.2f\n", i, b.x, b.y);
        g_failures++;
        break;
      }
    }
    check("after ten seconds of shaking it is still on the glass",
          b.x >= BUDDY_BALL_RADIUS - 0.01f && b.x <= BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS + 0.01f &&
              b.y >= BUDDY_BALL_RADIUS - 0.01f && b.y <= BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS + 0.01f);
    check("and it is still alive", b.active);
  }

  printf("-- the gaze follows it\n");
  {
    BuddyBall b = restBall();
    b.y = BUDDY_BALL_SCREEN_H * 0.5f;
    float gx = 0, gy = 0;
    b.x = BUDDY_BALL_RADIUS;
    buddyBallGaze(b, &gx, &gy);
    check("a ball on the left pulls the gaze left", gx < -10.0f);
    check("the horizontal gaze stays in reach", fabsf(gx) <= 24.0f + 0.01f);
    b.x = BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS;
    buddyBallGaze(b, &gx, &gy);
    check("a ball on the right pulls the gaze right", gx > 10.0f);
    check("the horizontal reach is symmetric", fabsf(gx) <= 24.0f + 0.01f);
    b.x = BUDDY_BALL_SCREEN_W * 0.5f;
    b.y = BUDDY_BALL_RADIUS;
    buddyBallGaze(b, &gx, &gy);
    check("a centered ball at the top looks up", gy < -8.0f);
    check("the vertical gaze stays in reach", fabsf(gy) <= 16.0f + 0.01f);
    b.y = BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS;
    buddyBallGaze(b, &gx, &gy);
    check("a ball at the bottom looks down", gy > 8.0f);
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall buddy_ball checks passed\n");
  return 0;
}
