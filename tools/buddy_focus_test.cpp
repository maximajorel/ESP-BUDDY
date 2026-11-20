// Host-side test for the study countdown (Concentración, S4/S7/S8).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_focus_test.cpp -o /tmp/bfocus && /tmp/bfocus
//
// The countdown must be a pure function of the board clock: the remaining time
// is derived from an absolute deadline, never accumulated per tick, so a bridge
// phase change, a mood change or a whole screen switch cannot reset it, slow it
// or lose it. Ticks can stop for minutes and the clock still tells the truth.

#include <cstdio>
#include <cstring>

#include "buddy_focus.h"

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
  printf("-- start\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    check("a cleared timer is idle", !f.active && !f.finished);
    check("idle remaining is zero", buddyFocusRemainingMs(&f, 1000) == 0);
    check("idle progress is zero", buddyFocusProgress(&f, 1000) == 0);

    check("starting 25 minutes is accepted", buddyFocusStart(&f, 5000, 25));
    check("it is active and not finished", f.active && !f.finished);
    check("the duration is the requested one",
          buddyFocusRemainingMs(&f, 5000) == 25u * 60000u);
    check("the default is the documented one", BUDDY_FOCUS_DEFAULT_MINUTES == 25);
  }

  printf("-- the clock, not the ticks\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    buddyFocusStart(&f, 1000, 25);
    // One tick after two minutes: two minutes are gone, not one frame.
    buddyFocusTick(&f, 1000 + 120000);
    check("a late tick still spends real time",
          buddyFocusRemainingMs(&f, 1000 + 120000) == 23u * 60000u);
    // No tick at all for nine minutes: the deadline is still the start.
    check("a long silence does not restart the countdown",
          buddyFocusRemainingMs(&f, 1000 + 540000) == 16u * 60000u);
    buddyFocusTick(&f, 1000 + 540000);
    check("after the silence it is still the same countdown",
          buddyFocusRemainingMs(&f, 1000 + 540000) == 16u * 60000u);
  }

  printf("-- finishing\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    buddyFocusStart(&f, 0, 1);
    check("before the end it is active", buddyFocusRemainingMs(&f, 59999) == 1);
    check("at the end the remaining is zero", buddyFocusRemainingMs(&f, 60000) == 0);
    check("before the tick it is not finished yet", !f.finished);
    buddyFocusTick(&f, 60000);
    check("the tick at zero finishes it", !f.active && f.finished);
    check("the finished timer reports zero remaining", buddyFocusRemainingMs(&f, 61000) == 0);
    check("the celebration is still visible right away",
          buddyFocusDoneVisible(&f, 60000));
    check("the celebration lasts its window",
          buddyFocusDoneVisible(&f, 60000 + BUDDY_FOCUS_DONE_MS - 1));
    check("the celebration ends after its window",
          !buddyFocusDoneVisible(&f, 60000 + BUDDY_FOCUS_DONE_MS));
    buddyFocusTick(&f, 60000 + BUDDY_FOCUS_DONE_MS);
    check("one tick past the window clears it", !f.finished);
  }

  printf("-- restart and cancel\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    buddyFocusStart(&f, 0, 25);
    buddyFocusStart(&f, 60000, 5);
    check("a new start replaces the countdown",
          buddyFocusRemainingMs(&f, 60000) == 5u * 60000u);
    check("a new start is not a finish", f.active && !f.finished);
    check("zero minutes cancels", buddyFocusStart(&f, 120000, 0));
    check("the cancel leaves it idle", !f.active && !f.finished);
    check("the cancel leaves no remaining", buddyFocusRemainingMs(&f, 120000) == 0);
  }

  printf("-- bounds\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    buddyFocusStart(&f, 0, 1);
    check("one minute is the floor", buddyFocusRemainingMs(&f, 0) == 60000u);
    check("above the ceiling it clamps", buddyFocusStart(&f, 0, 999));
    check("the clamp is the documented maximum",
          buddyFocusRemainingMs(&f, 0) == BUDDY_FOCUS_MAX_MINUTES * 60000u);
    check("the maximum is the documented one", BUDDY_FOCUS_MAX_MINUTES == 180);
  }

  printf("-- progress\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    buddyFocusStart(&f, 0, 10);
    check("progress starts at zero", buddyFocusProgress(&f, 0) == 0);
    check("progress is half way at half time", buddyFocusProgress(&f, 300000) == 127);
    check("progress is full at the deadline", buddyFocusProgress(&f, 600000) == 255);
    check("progress never passes full", buddyFocusProgress(&f, 900000) == 255);
  }

  printf("-- the label under the eyes\n");
  {
    char buf[8];
    buddyFocusFormat(0, buf, sizeof(buf));
    check("zero prints as 00:00", strcmp(buf, "00:00") == 0);
    buddyFocusFormat(25000, buf, sizeof(buf));
    check("a full block prints as mm:ss", strcmp(buf, "00:25") == 0);
    buddyFocusFormat(61000, buf, sizeof(buf));
    check("minutes and seconds print together", strcmp(buf, "01:01") == 0);
    buddyFocusFormat(25u * 60000u, buf, sizeof(buf));
    check("the default duration prints as 25:00", strcmp(buf, "25:00") == 0);
    buddyFocusFormat(180u * 60000u, buf, sizeof(buf));
    check("three digit minutes still print", strcmp(buf, "180:00") == 0);
    buddyFocusFormat(59999, buf, sizeof(buf));
    check("a partial second rounds up so it never lies at zero",
          strcmp(buf, "01:00") == 0);
  }

  printf("-- the millis wrap\n");
  {
    BuddyFocus f;
    buddyFocusClear(&f);
    uint32_t start = 0xFFFFFF00u;  // 256 ms before millis() wraps
    buddyFocusStart(&f, start, 1);
    check("a countdown across the wrap keeps its remaining",
          buddyFocusRemainingMs(&f, start + 59000u) == 1000);
    buddyFocusTick(&f, start + 59000u);
    check("a countdown across the wrap is still active", f.active);
    check("a countdown across the wrap still finishes",
          buddyFocusRemainingMs(&f, start + 60000u) == 0);
    buddyFocusTick(&f, start + 60000u);
    check("it finishes after the wrap", f.finished);
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall buddy_focus checks passed\n");
  return 0;
}
