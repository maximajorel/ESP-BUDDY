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
  check("dizziness is not anger", !p.angry);
  check("dizzy does not tremble", p.tremble == 0.0f);
  check("dizzy is not angry and does not grow", p.grow == 1.0f);
  // Keep the dizzy face: once the second shake escalates, the state machine no
  // longer answers for the earlier moment.
  BuddyImuPose dizzyFace = p;
  {
    BuddyImuPose a = buddyImuPose(&sh, 1200);
    BuddyImuPose b = buddyImuPose(&sh, 1500);
    check("the dizzy eyes orbit", fabsf(a.gazeX - b.gazeX) > 0.5f ||
                                      fabsf(a.gazeY - b.gazeY) > 0.5f);
  }
  p = buddyImuPose(&sh, 3000);
  check("dizzy expires on its own", !p.dizzy);

  printf("-- shake while dizzy makes it angry\n");
  for (uint32_t t = 3100; t <= 3400; t += 100) feed(sh, 0, 0, 3.0f, t, 100);
  p = buddyImuPose(&sh, 3400);
  check("shaking a dizzy face makes it angry", p.angry);
  check("the angry face drops the top lids", p.lid >= 0.3f);
  check("the angry face glows hot", p.hot >= 0.5f);
  check("the inner corners drop", p.rotL > 0.0f && p.rotR < 0.0f);
  check("anger replaces the dizzy wobble", !p.dizzy);
  check("anger grows the eyes", p.grow > 1.05f && p.grow <= 1.3f);
  check("anger turns the eyes fully red", p.hot > 0.95f);
  check("the angry eyelid is high enough to read as a block, not a slit",
        p.lid >= 0.4f);
  check("anger trembles", p.tremble >= 0.8f);
  check("anger does not drop a tired bottom lid", p.bottom <= 0.01f);
  check("the two faces do not share their body",
        dizzyFace.grow == 1.0f && dizzyFace.tremble == 0.0f &&
            p.grow > dizzyFace.grow && p.tremble > dizzyFace.tremble);
  check("anger turns the eyes further than dizziness",
        p.rotL > dizzyFace.rotL + 5.0f && p.rotR < dizzyFace.rotR - 5.0f);
  {
    float maxAbs = 0.0f, lo = 0.0f, hi = 0.0f;
    for (uint32_t t = 3400; t < 4100; t += 40) {
      float g = buddyImuPose(&sh, t).gazeX;
      if (fabsf(g) > maxAbs) maxAbs = fabsf(g);
      if (g < lo) lo = g;
      if (g > hi) hi = g;
    }
    check("the angry eyes slam toward a screen edge", maxAbs > 45.0f);
    check("the slam bounces both ways", hi > 40.0f && lo < -40.0f);
    check("the slam stays on the glass", maxAbs <= 56.0f);
  }
  p = buddyImuPose(&sh, 7000);
  check("anger expires", !p.angry);

  printf("-- repeated shakes anger it\n");
  BuddyImu ra;
  feed(ra, 0, 0, 1, 0, 100);
  for (uint32_t t = 100; t <= 400; t += 100) feed(ra, 0, 0, 3.0f, t, 100);
  check("the first burst is dizzy", buddyImuPose(&ra, 500).dizzy);
  for (uint32_t t = 3800; t <= 4100; t += 100) feed(ra, 0, 0, 3.0f, t, 100);
  p = buddyImuPose(&ra, 4100);
  check("a second burst inside the window is anger", p.angry && !p.dizzy);
  p = buddyImuPose(&ra, 9000);
  check("and it expires", !p.angry);

  printf("-- a sharp hit startles\n");
  BuddyImu st;
  feed(st, 0, 0, 1, 0, 100);
  feed(st, 0, 0, 3.6f, 100, 100);
  p = buddyImuPose(&st, 150);
  check("a hit opens the eyes wide", p.wide);
  p = buddyImuPose(&st, 800);
  check("startle expires", !p.wide);

  printf("-- lying flat, ZzZ and waking up\n");
  {
    // S15: face up on the table counts as lying, and the timer runs.
    BuddyImu ly;
    for (uint32_t t = 0; t <= 4000; t += 100) feed(ly, 0, 0, 1.0f, t, 100);
    p = buddyImuPose(&ly, 4000);
    check("resting face up is lying flat", p.lying && p.lyingMs >= 1000);
    // A real movement wakes it at once.
    feed(ly, 0, 0, 1.6f, 4100, 100);
    p = buddyImuPose(&ly, 4200);
    check("moving the board wakes it", p.wake);
    check("and stops counting the lying time", !p.lying || p.lyingMs < 200);
    BuddyImu fd2;
    for (uint32_t t = 0; t <= 3000; t += 100) feed(fd2, 0, 0, -1.0f, t, 100);
    p = buddyImuPose(&fd2, 3000);
    check("glass down on the table is lying too", p.lying && p.lyingMs >= 1000);
    BuddyImu up;
    for (uint32_t t = 0; t <= 3000; t += 100) feed(up, 0.55f, 0, 0.55f, t, 100);
    p = buddyImuPose(&up, 3000);
    check("leaning on a support is not lying down", !p.lying);
  }

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
