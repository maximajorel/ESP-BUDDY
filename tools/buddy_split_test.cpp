// Host-side test for the split glass (S1/S2 of odd/tasks/temporizador-partido.md).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_split_test.cpp -o /tmp/bsplit && /tmp/bsplit
//
// The calendar event and the Concentration timer share one split frame: the real
// face on the left, a data column on the right, a one-pixel rule between them.
// The numbers are the contract, so they live in a pure header and are pinned
// here: no board, no pixels, just the arithmetic a bad edit would break.

#include <cstdio>
#include <cmath>

#include "buddy_face.h"
#include "buddy_split.h"

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
  printf("-- the two panes\n");
  {
    check("the divider is inside the glass",
          BUDDY_SPLIT_DIVIDER_X > 0 && BUDDY_SPLIT_DIVIDER_X < BUDDY_SPLIT_W);
    check("the content column starts right of the divider",
          BUDDY_SPLIT_CONTENT_X > BUDDY_SPLIT_DIVIDER_X + 1);
    check("the content column never leaves the glass",
          BUDDY_SPLIT_CONTENT_X + BUDDY_SPLIT_CONTENT_W <= BUDDY_SPLIT_W);
    check("the rule spans the same band the calendar used",
          BUDDY_SPLIT_RULE_Y == 10 && BUDDY_SPLIT_RULE_H == 152);
  }

  printf("-- the face frame\n");
  {
    BuddySplitFaceFrame f = buddySplitFaceFrame();
    check("the pair is centred in the left pane",
          (f.cx[0] + f.cx[1]) / 2 == BUDDY_SPLIT_FACE_CX);
    check("the left eye is left of the right eye", f.cx[0] < f.cx[1]);
    check("the pair is symmetric around the pane centre",
          BUDDY_SPLIT_FACE_CX - f.cx[0] == f.cx[1] - BUDDY_SPLIT_FACE_CX);
    check("the face is scaled down from full size",
          f.scale > 0.0f && f.scale < 1.0f);
    check("the vertical centre stays on the glass",
          f.cy > 0 && f.cy < BUDDY_SPLIT_H);
  }

  printf("-- every real expression fits the left pane\n");
  {
    // The extremes the pose ladders can reach, from buddy_anim.h, buddy_imu.h
    // and buddy_upcomingPose: the widest, the tallest, the biggest grow and the
    // hardest tilt. None of them may cross the divider.
    struct Extreme {
      const char *name;
      float rx, ry, grow, deg;
    };
    const Extreme extremes[] = {
        {"neutral", 48.0f, 48.0f, 1.00f, 0.0f},
        {"upcoming, nearly now", 60.0f, 68.0f, 1.00f, 3.0f},
        {"event happening now", 56.0f, 70.0f, 1.00f, 1.5f},
        {"life, air", 48.0f, 55.0f, 1.035f, 5.0f},
        {"anger", 48.0f, 48.0f, 1.12f, 26.0f},
        {"game surprise", 56.0f, 70.0f, 1.12f, 8.0f},
    };
    const int dividerRoom = BUDDY_SPLIT_DIVIDER_X - BUDDY_SPLIT_FACE_MARGIN;
    for (const Extreme &e : extremes) {
      const float rx = e.rx * e.grow * BUDDY_SPLIT_FACE_SCALE;
      const float ry = e.ry * e.grow * BUDDY_SPLIT_FACE_SCALE;
      const float extent = buddyEyeExtentX(rx, ry, e.deg);
      const float extentY = buddyEyeExtentY(rx, ry, e.deg);
      const int outer =
          BUDDY_SPLIT_FACE_CX + BUDDY_SPLIT_EYE_HALF_GAP +
          (int)ceilf(extent) + BUDDY_GLOW_PAD;
      char what[96];
      snprintf(what, sizeof(what),
               "%s: the glow stops before the divider (outer %d <= %d)",
               e.name, outer, dividerRoom);
      check(what, outer <= dividerRoom);
      const int top = BUDDY_SPLIT_FACE_CY - (int)ceilf(extentY) - BUDDY_GLOW_PAD;
      const int bottom = BUDDY_SPLIT_FACE_CY + (int)ceilf(extentY) + BUDDY_GLOW_PAD;
      snprintf(what, sizeof(what),
               "%s: the glow stays between the top and bottom edges", e.name);
      check(what, top >= 0 && bottom <= BUDDY_SPLIT_H);
    }
  }

  printf("-- the pane shift helper\n");
  {
    check("a pair that already fits does not move",
          buddySplitPairShift(BUDDY_SPLIT_FACE_MARGIN, 120) == 0);
    check("a pair leaking off the left edge moves right",
          buddySplitPairShift(-4, 120) == BUDDY_SPLIT_FACE_MARGIN + 4);
    check("a pair leaking past the divider moves left",
          buddySplitPairShift(4, BUDDY_SPLIT_DIVIDER_X + 3) == -5);
    const int both = buddySplitPairShift(-6, BUDDY_SPLIT_DIVIDER_X + 6);
    check("a pair equidistant past both edges does not move", both == 0);
    check("a pair leaking more on one side leans that way",
          buddySplitPairShift(-2, BUDDY_SPLIT_DIVIDER_X + 8) < 0);
  }

  if (g_failures) {
    printf("%d failure(s)\n", g_failures);
    return 1;
  }
  printf("all split-frame checks passed\n");
  return 0;
}
