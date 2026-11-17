// Host-side test for the physical reactions (Sofía's firmware task 4).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_imu_test.cpp -o /tmp/bit && /tmp/bit
//
// Pins the reaction state machine: a sustained shake makes it dizzy, a sharp
// hit startles it, face-down squints, a tilt leans the gaze, and garbage from a
// missing sensor never counts as a shake. Every reaction expires on its own.

#include <cstdio>
#include <math.h>

#include "buddy_imu.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void feed(BuddyImu &s, float ax, float ay, float az, uint32_t nowMs,
                 uint32_t dt) {
  float a[3] = {ax, ay, az};
  float g[3] = {0, 0, 0};
  buddyImuFeed(&s, a, g, nowMs, dt);
}

static void feedG(BuddyImu &s, float gx, uint32_t nowMs) {
  float a[3] = {0, 0, 1};
  float g[3] = {gx, 0, 0};
  buddyImuFeed(&s, a, g, nowMs, 100);
}

int main() {
  printf("-- no sample, upright, tilt\n");
  BuddyImu s;
  BuddyImuPose p = buddyImuPose(&s, 0);
  check("no sample is a neutral pose", !p.dizzy && !p.wide && p.gazeX == 0);
  feed(s, 0, 0, 1, 0, 100);
  p = buddyImuPose(&s, 0);
  check("upright is calm", !p.dizzy && !p.squint && fabsf(p.gazeX) < 0.5f);
  for (uint32_t t = 100; t <= 500; t += 100) feed(s, 0.6f, 0, 0.8f, t, 100);
  p = buddyImuPose(&s, 500);
  check("a tilt leans the gaze", fabsf(p.gazeX) > 2.0f);

  printf("-- sustained shake\n");
  BuddyImu sh;
  feed(sh, 0, 0, 1, 0, 100);
  for (uint32_t t = 100; t <= 400; t += 100) feed(sh, 0, 0, 3.0f, t, 100);
  p = buddyImuPose(&sh, 400);
  check("a 300 ms shake makes it dizzy", p.dizzy);
  check("the dizzy face drops its lids", p.lid >= 0.25f);
  p = buddyImuPose(&sh, 3000);
  check("dizzy expires on its own", !p.dizzy);

  printf("-- shake while dizzy is annoyance\n");
  for (uint32_t t = 500; t <= 800; t += 100) feed(sh, 0, 0, 3.0f, t, 100);
  p = buddyImuPose(&sh, 800);
  check("shaking a dizzy face annoys it", p.squint);
  p = buddyImuPose(&sh, 2500);
  check("annoyance expires", !p.squint);

  printf("-- a sharp hit startles\n");
  BuddyImu st;
  feed(st, 0, 0, 1, 0, 100);
  feed(st, 0, 0, 3.6f, 100, 100);
  p = buddyImuPose(&st, 150);
  check("a hit opens the eyes wide", p.wide);
  p = buddyImuPose(&st, 800);
  check("startle expires", !p.wide);

  printf("-- face down and garbage\n");
  BuddyImu fd;
  for (uint32_t t = 0; t <= 300; t += 100) feed(fd, 0, 0, -1.0f, t, 100);
  p = buddyImuPose(&fd, 300);
  check("face down squints", p.squint && p.faceDown);
  BuddyImu g;
  feed(g, 0, 0, 1, 0, 100);  // valid baseline
  feed(g, 0, 0, 0, 100, 100);
  check("a zero reading is ignored, not a shake", buddyImuPose(&g, 200).dizzy == false);
  float nan = NAN;
  float bad[3] = {nan, nan, nan};
  float zeroG[3] = {0, 0, 0};
  buddyImuFeed(&g, bad, zeroG, 200, 100);
  check("NaN does not crash and does not shake", !buddyImuPose(&g, 200).dizzy);

  printf("-- follow, rhythmic shake, lift, soft hit\n");
  BuddyImu fo;
  feed(fo, 0, 0, 1, 0, 100);
  feedG(fo, 100, 100);
  p = buddyImuPose(&fo, 100);
  check("a quick turn pulls the gaze", fabsf(p.gazeX) > 1.0f);

  BuddyImu rh;
  feed(rh, 0, 0, 1, 0, 100);
  for (uint32_t t = 100; t <= 600; t += 100) feedG(rh, (t / 100) % 2 ? 100 : -100, t);
  p = buddyImuPose(&rh, 600);
  check("a soft rhythmic shake is pleasant", p.like);
  check("rhythmic is not dizzy", !p.dizzy);

  BuddyImu lf;
  feed(lf, 0, 0, 1, 0, 100);
  feed(lf, 0, 0, 1, 100, 100);
  for (uint32_t t = 200; t <= 500; t += 100) feed(lf, 0, 0, 0.3f, t, 100);
  p = buddyImuPose(&lf, 500);
  check("lifting it wakes the sleeping face", p.wake);

  BuddyImu sh2;
  feed(sh2, 0, 0, 1, 0, 100);
  feed(sh2, 0, 0, 2.0f, 100, 100);
  p = buddyImuPose(&sh2, 150);
  check("a soft landing is a short flinch", p.wide);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
