// Host-side test for the face geometry, owner colour and smoothing (S1, S2, S5).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_face_test.cpp -o /tmp/bft && /tmp/bft
//
// Pins the rounded-square test, the dim/hot derivations of the owner's colour,
// and the easing filter the 20 fps face relies on: it never overshoots, never
// slides in from zero on the first frame, and settles exactly on the target.

#include <cstdio>

#include "buddy_face.h"

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

int main() {
  printf("-- owner eye colour\n");
  check("the default is the shipped cyan",
        BUDDY_EYE_COLOR_DEFAULT == 0x00B4FFu);
  uint32_t dim = buddyEyeDim(BUDDY_EYE_COLOR_DEFAULT);
  check("dim keeps the hue and darkens",
        ((dim >> 16) & 0xFFu) == 0u && ((dim >> 8) & 0xFFu) > 40u &&
            ((dim & 0xFFu) < 255u) && dim != BUDDY_EYE_COLOR_DEFAULT);
  check("dim of black is black", buddyEyeDim(0x000000u) == 0x000000u);
  check("hot 0 is the owner colour unchanged",
        buddyEyeHot(0x123456u, 0.0f) == 0x123456u);
  check("hot clamps below zero", buddyEyeHot(0x123456u, -3.0f) == 0x123456u);
  check("hot 1 is the hot colour", buddyEyeHot(0x00B4FFu, 1.0f) == 0xFF3C1Eu);
  check("hot clamps above one", buddyEyeHot(0x00B4FFu, 9.0f) == 0xFF3C1Eu);
  check("white stays white when not hot",
        buddyEyeHot(0xFFFFFFu, 0.0f) == 0xFFFFFFu);
  check("hot moves a mid value towards red",
        buddyEyeHot(0x808080u, 0.5f) > 0x808080u);

  printf("-- rounded square\n");
  BuddySquircle s = buddySquircle(48.0f, 48.0f);
  checkNear("corner radius is a fraction of the short half-axis", s.radius,
            48.0f * BUDDY_EYE_CORNER_RATIO, 0.001f);
  check("the centre is inside", buddySquircleInside(0.0f, 0.0f, s));
  check("the middle of an edge is inside",
        buddySquircleInside(0.0f, 47.0f, s) && buddySquircleInside(47.0f, 0.0f, s));
  check("the straight band is inside up to the edge",
        buddySquircleInside(30.0f, 47.0f, s));
  check("outside the box is outside",
        !buddySquircleInside(49.0f, 0.0f, s) && !buddySquircleInside(0.0f, -49.0f, s));
  check("the cut corner is outside",
        !buddySquircleInside(47.5f, 47.5f, s));
  check("just inside the corner arc is inside",
        buddySquircleInside(46.0f, 30.0f, s));
  check("a square keeps a corner radius below the half-axis",
        buddySquircle(10.0f, 60.0f).radius < 10.0f);

  printf("-- eye separation\n");
  check("a wide open pair keeps its gap",
        buddyEyeGapPush(110.0f, 48.0f, 48.0f, BUDDY_EYE_GAP_MIN) == 0);
  check("surprise width is pushed apart to keep the gap",
        buddyEyeGapPush(110.0f, 54.0f, 54.0f, BUDDY_EYE_GAP_MIN) > 0);
  check("a tilted eye occupies more width",
        buddyEyeExtentX(48.0f, 48.0f, 8.0f) > 48.0f);
  {
    int push = buddyEyeGapPush(110.0f, 54.0f, 54.0f, BUDDY_EYE_GAP_MIN);
    float gap = 110.0f + 2.0f * (float)push - 108.0f;
    check("the push restores the minimum gap", gap >= BUDDY_EYE_GAP_MIN);
  }
  check("the cross-eyed error keeps a gap too",
        buddyEyeGapPush(96.0f, 48.0f, 48.0f, BUDDY_EYE_GAP_MIN) > 0);

  printf("-- easing\n");
  checkNear("no time means no movement", buddyEase(2.0f, 9.0f, 0, 100.0f),
            2.0f, 0.0001f);
  checkNear("a zero tau snaps", buddyEase(2.0f, 9.0f, 50, 0.0f), 9.0f, 0.0001f);
  float v = buddyEase(0.0f, 10.0f, 100, 100.0f);
  check("one tau covers about 63%", v > 6.0f && v < 6.8f);
  check("it never overshoots", buddyEase(9.0f, 10.0f, 100000, 100.0f) <= 10.0f);
  check("a large dt settles on the target",
        buddyEase(0.0f, 10.0f, 100000, 100.0f) > 9.99f);

  printf("-- pose filter\n");
  BuddyPoseFilter f;
  buddyPoseFilterReset(&f);
  BuddyPose target;
  target.gazeX = 12.0f;
  target.ry = 30;
  target.blink = true;
  BuddyPose out = buddyPoseFilterStep(&f, target, 50);
  check("the first frame adopts the target", out.gazeX == 12.0f && out.ry == 30);
  check("booleans snap", out.blink);

  BuddyPose next;
  next.gazeX = -12.0f;
  next.ry = 54;
  out = buddyPoseFilterStep(&f, next, 0);
  check("dt 0 keeps the previous eased value", out.gazeX == 12.0f);
  for (int i = 0; i < 20; i++) out = buddyPoseFilterStep(&f, next, 50);
  check("it converges towards the new target",
        out.gazeX < -11.0f && out.ry >= 53);
  check("and does not overshoot", out.gazeX >= -12.0f && out.ry <= 54);

  buddyPoseFilterReset(&f);
  BuddyPose zero;
  zero.gazeX = 5.0f;
  out = buddyPoseFilterStep(&f, zero, 50);
  check("a reset re-adopts instead of easing from stale values",
        out.gazeX == 5.0f);

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall buddy_face checks passed\n");
  return 0;
}
