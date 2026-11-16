// Host-side test for the tap window: one tap or two?
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/tap_window_test.cpp -o /tmp/twt && /tmp/twt
//
// The game has one button and two gestures on it: a single tap starts the run and
// a double tap opens the calibration prompt. Both must be decided from the same
// press stream without the first tap starting a game the second tap wanted to
// cancel, so the rule is a tiny pure state machine pinned here.

#include <cstdio>
#include <cstdint>

#include "tap_window.h"

static int g_failures = 0;

static void check(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-56s got=%d want=%d\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-56s %d\n", what, got);
  }
}

static const char *name(TapResult r) {
  switch (r) {
    case TAP_NONE: return "NONE";
    case TAP_WAIT: return "WAIT";
    case TAP_SINGLE: return "SINGLE";
    case TAP_DOUBLE: return "DOUBLE";
  }
  return "?";
}

static void expectTap(const char *what, TapResult got, TapResult want) {
  if (got != want) {
    printf("FAIL %-56s got=%s want=%s\n", what, name(got), name(want));
    g_failures++;
  } else {
    printf("ok   %-56s %s\n", what, name(got));
  }
}

int main() {
  const uint32_t W = 400;
  TapWindow w;

  tapWindowReset(w);
  expectTap("a fresh window has nothing to resolve", tapWindowPoll(w, 1000, W),
            TAP_NONE);

  expectTap("the first press opens the window", tapWindowPress(w, 1000, W),
            TAP_NONE);
  expectTap("nothing resolves before the window elapses",
            tapWindowPoll(w, 1200, W), TAP_WAIT);

  expectTap("a second press inside the window is a double",
            tapWindowPress(w, 1300, W), TAP_DOUBLE);
  expectTap("the double closes the window", tapWindowPoll(w, 1301, W), TAP_NONE);

  tapWindowReset(w);
  tapWindowPress(w, 2000, W);
  expectTap("the wait is over at the window edge",
            tapWindowPoll(w, 2000 + W, W), TAP_SINGLE);
  expectTap("a single is reported only once", tapWindowPoll(w, 2500, W),
            TAP_NONE);

  tapWindowReset(w);
  tapWindowPress(w, 3000, W);
  // A press that arrives late, without an intervening poll, starts a new window
  // instead of counting as the second half of a double.
  expectTap("a press after the window starts over",
            tapWindowPress(w, 3000 + W + 1, W), TAP_NONE);
  expectTap("the late press is now the first of a new pair",
            tapWindowPress(w, 3000 + W + 100, W), TAP_DOUBLE);

  tapWindowReset(w);
  // millis() wraps about every 49 days; the comparison must survive it.
  tapWindowPress(w, 0xFFFFFF00u, W);
  expectTap("a double across the millisecond wrap",
            tapWindowPress(w, 0x00000050u, W), TAP_DOUBLE);

  tapWindowReset(w);
  tapWindowPress(w, 5000, W);
  tapWindowReset(w);
  expectTap("a reset drops a pending tap", tapWindowPoll(w, 5000 + W, W),
            TAP_NONE);

  check("the shipped window is long enough to be a deliberate double",
        TAP_DOUBLE_MS >= 250, 1);
  check("the shipped window is short enough not to delay a tap",
        TAP_DOUBLE_MS <= 450, 1);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
