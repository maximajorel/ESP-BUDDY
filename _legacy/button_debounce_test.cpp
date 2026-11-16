// Host-side test for the BOOT button debounce state machine.
//
//   g++ -std=c++17 -O1 -o /tmp/bdt button_debounce_test.cpp && /tmp/bdt
//
// Exercises the exact header the firmware includes, so the accepted-press
// rules are checked without a finger on the board. It says nothing about the
// wiring, only about the logic.

#include <cstdio>
#include <cstdlib>

#include "particle_lab/button_debounce.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-46s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-46s %d\n", what, (int)got);
  }
}

static int countPresses(ButtonDebounce &b, bool high, uint32_t from, uint32_t to,
                        uint32_t step) {
  int n = 0;
  for (uint32_t t = from; t <= to; t += step) {
    if (b.update(high, t)) n++;
  }
  return n;
}

int main() {
  // 1. Idle: nothing should ever fire.
  {
    ButtonDebounce b;
    check("idle for 5 s produces no press", countPresses(b, true, 0, 5000, 10) == 0, true);
  }

  // 2. A clean press held for a while fires exactly once.
  {
    ButtonDebounce b;
    b.update(false, 100);            // contact closes
    int n = countPresses(b, false, 110, 2000, 10);
    check("press held 2 s fires exactly once", n == 1, true);
  }

  // 3. A pulse shorter than the stable window is treated as bounce.
  {
    ButtonDebounce b;
    b.update(false, 100);
    int n = countPresses(b, false, 110, 120, 5);  // releases at 125
    n += countPresses(b, true, 125, 400, 5);
    check("20 ms pulse is ignored as bounce", n == 0, true);
  }

  // 4. Real bounce before settling must not produce extra presses.
  {
    ButtonDebounce b;
    int n = 0;
    n += b.update(false, 100);  // low
    n += b.update(true, 105);   // bounces high
    n += b.update(false, 110);  // low again
    n += b.update(true, 115);   // bounces high
    n += b.update(false, 120);  // settles low
    n += countPresses(b, false, 130, 2000, 10);
    check("bouncing press fires exactly once", n == 1, true);
  }

  // 5. Releasing and pressing again is a second, separate press.
  {
    ButtonDebounce b;
    int n = 0;
    n += b.update(false, 100);
    n += countPresses(b, false, 140, 400, 10);  // press 1 accepted here
    n += countPresses(b, true, 410, 600, 10);   // released
    n += b.update(false, 700);
    n += countPresses(b, false, 740, 1000, 10); // press 2 accepted here
    check("two separate presses fire twice", n == 2, true);
  }

  // 6. A button already held at boot must not fire on every poll.
  {
    ButtonDebounce b;
    int n = 0;
    n += b.update(false, 0);
    n += countPresses(b, false, 0, 3000, 10);
    check("held from t=0 fires at most once", n <= 1, true);
  }

  printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "PASSED", g_failures,
         g_failures == 1 ? "" : "s");
  return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
