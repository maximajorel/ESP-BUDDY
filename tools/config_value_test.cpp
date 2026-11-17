// Host-side test for the config screen's value rules.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/config_value_test.cpp -o /tmp/cvt && /tmp/cvt
//
// The CONFIG screen hands each brightness to two buttons, so the arithmetic is
// the whole behaviour: a `+` at the ceiling must stay there (the old wrap landed
// on the LCD floor, which reads as a screen that went dark), a `−` at the floor
// must stay there, a value that reached storage some other way must be repaired
// by one tap instead of stepped, and the bead's 0..255 level has to survive a
// trip through the percentage the glass and the panel both show.

#include <cstdio>

#include "config_value.h"

static int g_failures = 0;

static void checkInt(const char *what, int got, int want) {
  if (got != want) {
    printf("FAIL %-56s got=%d want=%d\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-56s %d\n", what, got);
  }
}

int main() {
  printf("-- LCD percent, one step up (floor 5, ceiling 100, step 10)\n");
  checkInt("5 -> 15", cfgStepUp(5, 5, 100, 10), 15);
  checkInt("90 -> 100", cfgStepUp(90, 5, 100, 10), 100);
  checkInt("95 -> 100, not past it", cfgStepUp(95, 5, 100, 10), 100);
  checkInt("100 -> 100, it saturates", cfgStepUp(100, 5, 100, 10), 100);
  checkInt("below the floor is repaired to the floor", cfgStepUp(3, 5, 100, 10),
           5);
  checkInt("above the ceiling is repaired to the ceiling",
           cfgStepUp(200, 5, 100, 10), 100);
  checkInt("a step that fits lands exactly on the ceiling",
           cfgStepUp(80, 5, 100, 20), 100);
  checkInt("a zero step changes nothing", cfgStepUp(40, 5, 100, 0), 40);
  checkInt("an inverted range answers the floor", cfgStepUp(40, 100, 5, 10), 100);

  printf("-- LCD percent, one step down\n");
  checkInt("100 -> 90", cfgStepDown(100, 5, 100, 10), 90);
  checkInt("15 -> 5", cfgStepDown(15, 5, 100, 10), 5);
  checkInt("10 -> 5, not below it", cfgStepDown(10, 5, 100, 10), 5);
  checkInt("5 -> 5, it saturates", cfgStepDown(5, 5, 100, 10), 5);
  checkInt("below the floor is repaired to the floor",
           cfgStepDown(1, 5, 100, 10), 5);
  checkInt("above the ceiling is repaired to the ceiling",
           cfgStepDown(200, 5, 100, 10), 100);
  checkInt("an inverted range answers the floor", cfgStepDown(40, 100, 5, 10),
           100);

  printf("-- the walk a held button produces stays inside the range\n");
  {
    uint8_t v = 90;
    int escapes = 0;
    for (int i = 0; i < 8; i++) {
      v = cfgStepUp(v, 5, 100, 10);
      if (v < 5 || v > 100) escapes++;
    }
    checkInt("8 taps up never leave 5..100", escapes, 0);
    checkInt("...and rest at the ceiling", v, 100);

    escapes = 0;
    for (int i = 0; i < 12; i++) {
      v = cfgStepDown(v, 5, 100, 10);
      if (v < 5 || v > 100) escapes++;
    }
    checkInt("12 taps down never leave 5..100", escapes, 0);
    checkInt("...and rest at the floor", v, 5);
  }

  printf("-- LED percent, one step up (floor 0, ceiling 100, step 10)\n");
  checkInt("0 -> 10", cfgStepUp(0, 0, 100, 10), 10);
  checkInt("90 -> 100", cfgStepUp(90, 0, 100, 10), 100);
  checkInt("100 -> 100, it saturates", cfgStepUp(100, 0, 100, 10), 100);
  checkInt("above the ceiling is repaired to the ceiling",
           cfgStepUp(250, 0, 100, 10), 100);

  printf("-- LED percent, one step down\n");
  checkInt("100 -> 90", cfgStepDown(100, 0, 100, 10), 90);
  checkInt("10 -> 0", cfgStepDown(10, 0, 100, 10), 0);
  checkInt("0 -> 0, fully dark is a value, not a floor to cross",
           cfgStepDown(0, 0, 100, 10), 0);
  checkInt("above the ceiling is repaired to the ceiling",
           cfgStepDown(250, 0, 100, 10), 100);

  printf("-- percent <-> level mapping\n");
  checkInt("0% is dark", cfgPercentToLevel(0), 0);
  checkInt("100% is full", cfgPercentToLevel(100), 255);
  checkInt("50% rounds up", cfgPercentToLevel(50), 128);
  checkInt("10% rounds up", cfgPercentToLevel(10), 26);
  checkInt("level 0 is 0%", cfgLevelToPercent(0), 0);
  checkInt("level 255 is 100%", cfgLevelToPercent(255), 100);
  checkInt("level 128 is 50%", cfgLevelToPercent(128), 50);
  checkInt("level 26 is 10%", cfgLevelToPercent(26), 10);

  printf("-- the round trip every reachable LED percent takes\n");
  {
    int bad = 0;
    for (int pct = 0; pct <= 100; pct += 10) {
      int back = cfgLevelToPercent(cfgPercentToLevel((uint8_t)pct));
      if (back != pct) {
        printf("FAIL %d%% came back as %d%%\n", pct, back);
        bad++;
      }
    }
    checkInt("all eleven steps survive the round trip", bad, 0);
  }

  printf("-- clamping a value that reached storage some other way\n");
  checkInt("0 becomes the LCD floor", cfgClamp(0, 5, 100), 5);
  checkInt("150 becomes the ceiling", cfgClamp(150, 5, 100), 100);
  checkInt("60 stays 60", cfgClamp(60, 5, 100), 60);
  checkInt("level 300 clamps to 255", cfgClamp(300, 0, 255), 255);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
