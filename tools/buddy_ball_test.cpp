// Host-side test for the idle ball (S2, S3).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_ball_test.cpp -o /tmp/bbt && /tmp/bbt
//
// Pins the physics the glass relies on: the ball never leaves the playfield, a
// shake pushes it (screen x mirrors the IMU x), walls bounce it and eat speed,
// friction settles it, the speed is capped, and the gaze mapping keeps the eyes
// on the ball but inside their reach.

#include <cstdio>

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

int main() {
  printf("-- spawn\n");
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
  }

  printf("-- clear\n");
  {
    BuddyBall b;
    buddyBallSpawn(&b, 1);
    buddyBallClear(&b);
    check("clearing stops the ball", !b.active && b.vx == 0.0f && b.vy == 0.0f);
    float x = b.x;
    buddyBallStep(&b, 4.0f, 4.0f, 50);
    check("a cleared ball never moves", b.x == x);
  }

  printf("-- a shake pushes it, screen x mirrors the IMU x\n");
  {
    BuddyBall b;
    b.active = true;
    b.x = 160;
    b.y = 86;
    buddyBallStep(&b, 2.0f, 0.0f, 100);
    check("a positive IMU x pushes the ball left on screen", b.vx < -10.0f);
    BuddyBall c;
    c.active = true;
    c.x = 160;
    c.y = 86;
    buddyBallStep(&c, 0.0f, 2.0f, 100);
    check("a positive IMU y pushes the ball down on screen", c.vy > 10.0f);
    BuddyBall still;
    still.active = true;
    still.x = 160;
    still.y = 86;
    buddyBallStep(&still, 0.0f, 0.0f, 100);
    check("no dynamic accel means no push", still.vx == 0.0f && still.vy == 0.0f);
  }

  printf("-- the edges bounce and eat speed\n");
  {
    BuddyBall b;
    b.active = true;
    b.x = BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS - 2.0f;
    b.y = 86;
    b.vx = 200.0f;
    buddyBallStep(&b, 0.0f, 0.0f, 50);
    check("hitting the right wall clamps the ball inside",
          b.x <= BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS);
    check("hitting the right wall flips the velocity", b.vx < 0.0f);
    check("the bounce loses energy",
          fabsf(b.vx) < 200.0f * BUDDY_BALL_RESTITUTION + 20.0f);

    BuddyBall t;
    t.active = true;
    t.x = 160;
    t.y = BUDDY_BALL_RADIUS + 2.0f;
    t.vy = -300.0f;
    buddyBallStep(&t, 0.0f, 0.0f, 50);
    check("the top wall clamps and flips too",
          t.y >= BUDDY_BALL_RADIUS && t.vy > 0.0f);
  }

  printf("-- it never escapes\n");
  {
    BuddyBall b;
    buddyBallSpawn(&b, 77);
    bool inside = true;
    for (int i = 0; i < 400; i++) {
      float ax = (i % 7 < 4) ? 3.0f : -3.0f;
      float ay = (i % 5 < 2) ? 2.5f : -2.5f;
      buddyBallStep(&b, ax, ay, 50);
      if (b.x < BUDDY_BALL_RADIUS - 0.01f ||
          b.x > BUDDY_BALL_SCREEN_W - BUDDY_BALL_RADIUS + 0.01f ||
          b.y < BUDDY_BALL_RADIUS - 0.01f ||
          b.y > BUDDY_BALL_SCREEN_H - BUDDY_BALL_RADIUS + 0.01f) {
        inside = false;
        break;
      }
    }
    check("a long violent shake keeps the ball on the glass", inside);
  }

  printf("-- friction and speed cap\n");
  {
    BuddyBall b;
    b.active = true;
    b.x = 160;
    b.y = 86;
    b.vx = 300.0f;
    buddyBallStep(&b, 0.0f, 0.0f, 500);
    check("friction slows a rolling ball", b.vx > 0.0f && b.vx < 300.0f);

    BuddyBall f;
    f.active = true;
    f.x = 160;
    f.y = 86;
    for (int i = 0; i < 30; i++) buddyBallStep(&f, 5.0f, 5.0f, 100);
    float speed = sqrtf(f.vx * f.vx + f.vy * f.vy);
    check("the speed stays capped", speed <= BUDDY_BALL_MAX_SPEED + 1.0f);
  }

  printf("-- the eyes follow it\n");
  {
    float gx = 0, gy = 0;
    BuddyBall b;
    b.active = true;
    b.x = BUDDY_BALL_RADIUS;
    b.y = 86;
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
