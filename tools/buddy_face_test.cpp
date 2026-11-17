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

  printf("-- glow shades\n");
  check("scaling 100% keeps the colour",
        buddyEyeScale(0x00B4FFu, 100) == 0x00B4FFu);
  check("scaling 0% is black", buddyEyeScale(0x00B4FFu, 0) == 0x000000u);
  check("the dim shade is a 32% scale",
        buddyEyeScale(0x00B4FFu, 32) == buddyEyeDim(0x00B4FFu));
  {
    uint32_t inner = buddyEyeScale(0x00B4FFu, 26);
    uint32_t outer = buddyEyeScale(0x00B4FFu, 12);
    check("the glow fades outward", inner > outer && outer > 0x000000u);
    check("the glow keeps the hue",
          ((inner >> 8) & 0xFFu) > ((inner >> 16) & 0xFFu));
  }

  printf("-- rotated extents\n");
  check("a rotated eye is taller than its box",
        buddyEyeExtentY(48.0f, 48.0f, 16.0f) > 48.0f);
  check("no rotation matches the box",
        fabsf(buddyEyeExtentY(48.0f, 48.0f, 0.0f) - 48.0f) < 0.01f);

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

  printf("-- lid bands cover the rotated eye and its glow\n");
  {
    // Angry: 16 deg tilt makes extentY ~59 for a 48 px box, lid 34%.
    int extentY = (int)ceilf(buddyEyeExtentY(48.0f, 48.0f, 16.0f));
    check("the angry rotation is taller than the box", extentY > 48);
    check("a lid line never leaves the eye",
          buddyLidScreenY(88, extentY, 0.34f, 0.0f, 0.0f) >= 88 - extentY);
    BuddyLidBand bot = buddyLidBandBottom(88, extentY, BUDDY_GLOW_PAD, 0.35f);
    check("the bottom band reaches below the glow",
          bot.y0 + bot.h - 1 >= 88 + extentY + BUDDY_GLOW_PAD);
    check("and starts at the bottom lid line",
          bot.y0 <= 88 + extentY - (int)(2.0f * extentY * 0.35f));
  }

  printf("-- lid line sub-pixel\n");
  {
    // S6: the band fill lands on a row; the renderer dithers the fraction so the
    // lid slides between rows instead of stepping.
    checkNear("a half lid lands on the centre row",
              buddyLidScreenY(88, 48, 0.5f, 0.0f, 0.0f), 88.0f, 0.001f);
    checkNear("an open lid is the top of the eye",
              buddyLidScreenY(88, 48, 0.0f, 0.0f, 0.0f), 40.0f, 0.001f);
    checkNear("the bottom lid mirrors it", buddyLidLineBottom(88, 48, 0.5f),
              88.0f, 0.001f);
    check("a fractional lid keeps its sub-pixel rest",
          fabsf(buddyLidScreenY(88, 48, 0.51f, 0.0f, 0.0f) - 88.96f) < 0.01f);
  }

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

  printf("-- idle perspective and tremble\n");
  {
    // S7: looking sideways is a small turn of the pair, not a slide.
    BuddyPerspective centre = buddyPerspective(0.0f);
    check("straight ahead is no perspective", centre.pushL == 0.0f &&
                                                   centre.pushR == 0.0f &&
                                                   centre.widenL == 1.0f &&
                                                   centre.widenR == 1.0f);
    BuddyPerspective right = buddyPerspective(18.0f);
    check("looking right leads with the right eye",
          right.pushR > right.pushL && right.pushR > 0.0f);
    check("the leading eye foreshortens", right.widenR < 1.0f);
    check("the trailing eye widens", right.widenL > 1.0f);
    BuddyPerspective left = buddyPerspective(-18.0f);
    check("looking left mirrors it", left.pushL < 0.0f &&
                                        left.pushL < left.pushR &&
                                        left.widenL < 1.0f && left.widenR > 1.0f);
    BuddyPerspective tooFar = buddyPerspective(200.0f);
    check("a wild gaze clamps to the same perspective",
          fabsf(tooFar.pushR - right.pushR) < 0.001f);

    // S5: the tremble is jitter applied after the ease, so the filter cannot
    // eat it.
    check("no tremble is no movement", buddyTremble(1234, 7, 1, 0.0f) == 0.0f);
    float lo = 0.0f, hi = 0.0f;
    bool changed = false;
    bool bounded = true;
    float prevTremble = buddyTremble(0, 7, 1, 2.0f);
    for (uint32_t t = 0; t < 1000; t += 40) {
      float v = buddyTremble(t, 7, 1, 2.0f);
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      if (fabsf(v - prevTremble) > 0.001f) changed = true;
      if (fabsf(v) > 2.001f) bounded = false;
      prevTremble = v;
    }
    check("the tremble stays inside its amplitude", bounded);
    check("the tremble keeps shaking", changed && lo < -0.5f && hi > 0.5f);
  }

  printf("-- the eyelid angles with the eye\n");
  {
    // S14/S17: no brows; the black eyelid block is the expression. A positively
    // rotated eye gets a lid that falls to the right, so the angry pair reads
    // "\/" when the two eyes rotate opposite ways.
    float leftInner = buddyLidScreenY(88, 48, 0.46f, 11.0f, 20.0f);
    float leftOuter = buddyLidScreenY(88, 48, 0.46f, 11.0f, -20.0f);
    float rightInner = buddyLidScreenY(88, 48, 0.46f, -11.0f, -20.0f);
    float rightOuter = buddyLidScreenY(88, 48, 0.46f, -11.0f, 20.0f);
    check("the left lid falls towards the nose", leftInner > leftOuter);
    check("the right lid falls towards the nose too", rightInner > rightOuter);
    check("an unrotated lid is flat",
          fabsf(buddyLidScreenY(88, 48, 0.46f, 0.0f, 20.0f) -
                buddyLidScreenY(88, 48, 0.46f, 0.0f, -20.0f)) < 0.01f);
    check("a wider lid cuts lower on the glass",
          buddyLidScreenY(88, 48, 0.8f, 0.0f, 0.0f) >
              buddyLidScreenY(88, 48, 0.2f, 0.0f, 0.0f));
  }

  printf("-- soft edges: coverage and ordered dither\n");
  {
    // S6: the panel has no alpha, so an edge pixel is either the eye colour or
    // the colour behind it, decided by a coverage value and a Bayer threshold.
    BuddySquircle sq = buddySquircle(48.0f, 48.0f);
    checkNear("the centre is fully covered",
              buddySquircleCoverage(0.0f, 0.0f, sq), 1.0f, 0.001f);
    checkNear("far outside is not covered",
              buddySquircleCoverage(80.0f, 0.0f, sq), 0.0f, 0.001f);
    float inside = buddySquircleCoverage(46.0f, 0.0f, sq);
    float edge = buddySquircleCoverage(48.2f, 0.0f, sq);
    check("a pixel on the edge is partly covered",
          inside >= edge && edge > 0.05f && edge < 0.6f);
    check("coverage falls off as you leave the shape",
          buddySquircleCoverage(47.0f, 0.0f, sq) >
              buddySquircleCoverage(48.5f, 0.0f, sq));
    check("the corner is covered less than the middle of the edge",
          buddySquircleCoverage(47.0f, 47.0f, sq) <
              buddySquircleCoverage(0.0f, 47.0f, sq));

    float b0 = buddyBayer4(0, 0);
    check("the dither is deterministic", b0 == buddyBayer4(4, 4));
    check("the dither stays in one step of 1/16", b0 >= 0.0f && b0 < 1.0f);
    check("the dither pattern has all sixteen levels",
          buddyBayer4(0, 0) != buddyBayer4(1, 0) &&
              buddyBayer4(1, 0) != buddyBayer4(2, 0) &&
              buddyBayer4(0, 1) != buddyBayer4(0, 0));
    int lit = 0;
    for (int y = 0; y < 4; y++) {
      for (int x = 0; x < 4; x++) {
        if (buddyDitherLit(0.5f, x, y)) lit++;
      }
    }
    check("half coverage lights about half a 4x4 tile", lit >= 6 && lit <= 10);
    check("full coverage is always lit", buddyDitherLit(1.0f, 1, 2));
    check("no coverage is never lit", !buddyDitherLit(0.0f, 1, 2));
  }

  printf("-- new pose channels default to the previous look\n");
  {
    BuddyPose base;
    check("no growth", base.grow == 1.0f);
    check("no tremble", base.tremble == 0.0f);
    BuddyPoseFilter f;
    buddyPoseFilterReset(&f);
    BuddyPose target;
    target.grow = 1.12f;
    target.tremble = 1.0f;
    BuddyPose out = buddyPoseFilterStep(&f, target, 50);
    check("the filter carries the new channels",
          out.grow == 1.12f && out.tremble == 1.0f);
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall buddy_face checks passed\n");
  return 0;
}
