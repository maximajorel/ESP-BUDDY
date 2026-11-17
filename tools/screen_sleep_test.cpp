// Host-side test for the auto-off rule.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/screen_sleep_test.cpp -o /tmp/slt && /tmp/slt
//
// Turning the glass and the LED off after a while is the kind of rule that is
// invisible until it misfires: a screen that should stay on during a game, or a
// clock that never sleeps. The decision is pure, so it is pinned here.

#include <cstdio>
#include <cstdint>

#include "screen_sleep.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-58s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-58s %d\n", what, (int)got);
  }
}

int main() {
  printf("-- which screens are included\n");
  const uint8_t mask = 0x09;  // clock (0) and network (3)
  check("clock is included", screenSleepIncluded(mask, 0), true);
  check("slot is not", screenSleepIncluded(mask, 1), false);
  check("game is not", screenSleepIncluded(mask, 2), false);
  check("network is included", screenSleepIncluded(mask, 3), true);
  check("music is not", screenSleepIncluded(mask, 4), false);
  check("a screen id past the mask byte is ignored",
        screenSleepIncluded(0xFF, 9), false);

  printf("-- zero seconds means never\n");
  check("no timeout at t=0", screenSleepDue(0, 0, 0), false);
  check("no timeout after an hour", screenSleepDue(3600000, 0, 0), false);

  printf("-- the boundary and the millis wrap\n");
  check("one millisecond before is not due", screenSleepDue(9999, 0, 10), false);
  check("at the second is due", screenSleepDue(10000, 0, 10), true);
  check("well past is due", screenSleepDue(30000, 0, 10), true);
  check("a later activity resets it", screenSleepDue(20000, 15000, 10), false);
  // last=0xFFFFF000, now=0x00001000: 8192 ms elapsed across the wrap.
  check("the wrap does not look like a lifetime",
        screenSleepDue(0x00001000u, 0xFFFFF000u, 5), true);
  check("the wrap under the threshold stays awake",
        screenSleepDue(0x00001000u, 0xFFFFF000u, 10), false);

  printf("-- an activity newer than the clock the caller read\n");
  // loop() reads millis() into `now`, then the button, console and network
  // handlers stamp their own, later millis(). The subtraction then wraps to ~49
  // days and the glass went dark on the spot: this is the CONFIG screen "turning
  // off when I change something".
  check("one millisecond ahead is not due", screenSleepDue(1000, 1001, 1), false);
  check("five milliseconds ahead is not due", screenSleepDue(1000, 1005, 1),
        false);
  check("half a second ahead is not due", screenSleepDue(1000, 1500, 1), false);
  check("one second ahead is not due", screenSleepDue(1000, 2000, 1), false);
  check("ahead across the wrap is not due",
        screenSleepDue(0xFFFFFFFEu, 0x00000002u, 1), false);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
