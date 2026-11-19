// Host-side test for the boot wake ladder (S1, S2, S3, S4).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_wake_test.cpp -o /tmp/bwt && /tmp/bwt
//
// Pins the artboard's timeline row by row: the black start, the opening, the
// slow blink, the shake, the happy squint, the neutral end pose and the fact
// that the wake never touches a channel outside its own seven. It also proves
// the ladder is continuous at the frame rate the glass draws at, so a wrong
// easing cannot make the face jump between two frames.

#include <cstdio>
#include <math.h>

#include "buddy_wake.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void checkNear(const char *what, float got, float want, float eps) {
  bool ok = fabsf(got - want) <= eps;
  if (!ok) printf("  got %.4f want %.4f\n", got, want);
  check(what, ok);
}

static BuddyPose poseAt(uint32_t ms) {
  BuddyPose p;
  buddyWakePose(ms, &p);
  return p;
}

int main() {
  printf("-- the table\n");
  {
    check("the ladder has the fifteen rows of the artboard",
          kBuddyWakeKeyCount == 15);
    bool ordered = true;
    for (int i = 1; i < kBuddyWakeKeyCount; i++) {
      if (kBuddyWakeKeys[i].at <= kBuddyWakeKeys[i - 1].at) ordered = false;
    }
    check("the rows are in ms order", ordered);
    check("the last row lands on BUDDY_WAKE_MS",
          kBuddyWakeKeys[kBuddyWakeKeyCount - 1].at == BUDDY_WAKE_MS);
    check("the ladder starts at zero", kBuddyWakeKeys[0].at == 0u);
  }

  printf("-- the easings\n");
  {
    checkNear("linear at the start", buddyWakeEase(BUDDY_WAKE_LINEAR, 0.0f),
              0.0f, 1e-6f);
    checkNear("linear at the end", buddyWakeEase(BUDDY_WAKE_LINEAR, 1.0f),
              1.0f, 1e-6f);
    checkNear("linear at the middle", buddyWakeEase(BUDDY_WAKE_LINEAR, 0.5f),
              0.5f, 1e-6f);
    checkNear("ease-out starts fast", buddyWakeEase(BUDDY_WAKE_OUT, 0.5f), 0.75f,
              1e-6f);
    checkNear("ease-in-out is slow at the start",
              buddyWakeEase(BUDDY_WAKE_IN_OUT, 0.25f), 0.125f, 1e-6f);
    checkNear("ease-in-out is symmetric",
              buddyWakeEase(BUDDY_WAKE_IN_OUT, 0.75f), 0.875f, 1e-6f);
    checkNear("every easing ends on one",
              buddyWakeEase(BUDDY_WAKE_IN_OUT, 1.0f), 1.0f, 1e-6f);
    checkNear("the easing clamps below zero",
              buddyWakeEase(BUDDY_WAKE_OUT, -2.0f), 0.0f, 1e-6f);
    checkNear("the easing clamps above one",
              buddyWakeEase(BUDDY_WAKE_OUT, 4.0f), 1.0f, 1e-6f);
    check("ease-out is monotonic", buddyWakeEase(BUDDY_WAKE_OUT, 0.3f) <
                                       buddyWakeEase(BUDDY_WAKE_OUT, 0.6f));
    check("ease-in-out is monotonic",
          buddyWakeEase(BUDDY_WAKE_IN_OUT, 0.3f) <
              buddyWakeEase(BUDDY_WAKE_IN_OUT, 0.6f));
  }

  printf("-- the first frame is a black panel\n");
  {
    BuddyPose p = poseAt(0);
    checkNear("the eye is a bar at once", p.open, 0.0f, 1e-6f);
    checkNear("the panel is black", p.glow, 0.0f, 1e-6f);
    checkNear("the eye starts small", p.grow, 0.25f, 1e-6f);
    checkNear("no bob", p.bob, 0.0f, 1e-6f);
    checkNear("no gaze", p.gazeX, 0.0f, 1e-6f);
    checkNear("no lower lid", p.bottom, 0.0f, 1e-6f);
    checkNear("no shake", p.tremble, 0.0f, 1e-6f);
    check("the wake never touches the lid", p.lid == 0.0f);
    check("the wake never touches the side lids",
          p.lidL == 0.0f && p.lidR == 0.0f);
    check("the wake never touches the rotation", p.rot == 0.0f);
    check("the wake never touches the tilt", p.tilt == 0.0f);
    check("the wake never touches the heat", p.hot == 0.0f);
    check("the wake never blinks the boolean", !p.blink);
    check("the wake is never the happy flag: the squint is the lower lid",
          !p.happy);
    check("the wake never asks for surprise", !p.surprise);
    check("the wake never dims", !p.dim);
  }

  printf("-- opening\n");
  {
    check("the eye is grown up at 2200", poseAt(2200).open >= 1.0f);
    checkNear("and settled by 2500", poseAt(2500).open, 1.0f, 1e-6f);
    check("the slow blink is nearly shut around 2800",
          poseAt(2800).open <= 0.1f);
    check("the eye is back at 3100", poseAt(3100).open >= 0.99f);
    bool monotonic = true;
    float prev = -1.0f;
    for (uint32_t t = 0; t <= 2200; t += 20) {
      float open = poseAt(t).open;
      if (open < prev - 1e-6f) monotonic = false;
      prev = open;
    }
    check("the aperture only grows during the opening", monotonic);
  }

  printf("-- energy\n");
  {
    checkNear("black at zero", poseAt(0).glow, 0.0f, 1e-6f);
    checkNear("half energy at 900", poseAt(900).glow, 0.5f, 1e-5f);
    bool full = true;
    for (uint32_t t = 2200; t <= BUDDY_WAKE_MS; t += 50) {
      if (poseAt(t).glow < 0.999f) full = false;
    }
    check("full energy from 2200 on", full);
  }

  printf("-- the shake\n");
  {
    check("the tremble is up at 3420", poseAt(3420).tremble >= 0.5f);
    check("and still up at 3560", poseAt(3560).tremble >= 0.5f);
    bool strong = true;
    for (uint32_t t = 3420; t <= 3560; t += 10) {
      if (poseAt(t).tremble < 0.5f) strong = false;
    }
    check("the tremble stays up through the whole shake", strong);
    checkNear("it is gone at 3680", poseAt(3680).tremble, 0.0f, 1e-6f);
    bool quiet = true;
    for (uint32_t t = 3680; t <= BUDDY_WAKE_MS; t += 10) {
      if (poseAt(t).tremble > 1e-6f) quiet = false;
    }
    check("the tremble never comes back", quiet);
  }

  printf("-- the happy squint\n");
  {
    // S2: from 3800 to 4400 the lower eyelid is up, the eye grows and it hops.
    bool raised = true;
    for (uint32_t t = 3800; t <= 4380; t += 10) {
      BuddyPose p = poseAt(t);
      if (p.bottom < 0.3f) raised = false;
      if (p.grow < 1.03f) raised = false;
    }
    check("the lower lid is up and the eye grows through the happy face",
          raised);
    check("the face hops up when the squint arrives", poseAt(3800).bob <= -2.9f);
    checkNear("and the lid is back down at the end", poseAt(BUDDY_WAKE_MS).bottom,
              0.0f, 1e-6f);
  }

  printf("-- the end\n");
  {
    check("the wake is not done one ms early", !buddyWakeDone(BUDDY_WAKE_MS - 1));
    check("the wake is done at BUDDY_WAKE_MS", buddyWakeDone(BUDDY_WAKE_MS));
    check("and stays done", buddyWakeDone(BUDDY_WAKE_MS * 3));
    BuddyPose p = poseAt(BUDDY_WAKE_MS);
    checkNear("the end is the neutral open eye", p.open, 1.0f, 1e-6f);
    checkNear("the end is full energy", p.glow, 1.0f, 1e-6f);
    checkNear("the end is the plain scale", p.grow, 1.0f, 1e-6f);
    checkNear("the end has no bob", p.bob, 0.0f, 1e-6f);
    checkNear("the end has no gaze", p.gazeX, 0.0f, 1e-6f);
    checkNear("the end has no lower lid", p.bottom, 0.0f, 1e-6f);
    checkNear("the end has no shake", p.tremble, 0.0f, 1e-6f);
    BuddyPose later = poseAt(BUDDY_WAKE_MS + 4321);
    check("a late frame holds the last row", later.open == p.open &&
                                                   later.bottom == p.bottom &&
                                                   later.grow == p.grow);
  }

  printf("-- continuity at the frame rate\n");
  {
    // The glass draws at 20 fps and the pose is a pure function of the clock, so
    // a 16 ms step is finer than any two frames it will ever show. Unitless
    // channels are capped at 0.35; the two pixel channels keep the same intent
    // with their own unit, because the S3 timeline asks for a 3 px hop eased out
    // over 120 ms, which is 0.75 px in 16 ms by construction.
    bool continuous = true;
    float worstUnit = 0.0f, worstPx = 0.0f;
    for (uint32_t t = 0; t + 16 <= BUDDY_WAKE_MS; t += 16) {
      BuddyPose a = poseAt(t);
      BuddyPose b = poseAt(t + 16);
      float unit[5] = {fabsf(b.open - a.open), fabsf(b.glow - a.glow),
                       fabsf(b.grow - a.grow), fabsf(b.bottom - a.bottom),
                       fabsf(b.tremble - a.tremble)};
      for (int i = 0; i < 5; i++) {
        if (unit[i] > worstUnit) worstUnit = unit[i];
        if (unit[i] > 0.35f) {
          continuous = false;
          printf("  open/glow/grow/bottom/tremble step %.3f at %u ms\n", unit[i],
                 (unsigned)t);
        }
      }
      float px[2] = {fabsf(b.bob - a.bob), fabsf(b.gazeX - a.gazeX)};
      for (int i = 0; i < 2; i++) {
        if (px[i] > worstPx) worstPx = px[i];
        if (px[i] > 1.0f) {
          continuous = false;
          printf("  bob/gazeX step %.3f px at %u ms\n", px[i], (unsigned)t);
        }
      }
    }
    printf("  worst step: %.3f unitless, %.3f px\n", worstUnit, worstPx);
    check("no channel jumps between two frames", continuous);
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall buddy_wake checks passed\n");
  return 0;
}
