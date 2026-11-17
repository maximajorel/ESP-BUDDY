// Host-side test for the config screen's value rules.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/config_value_test.cpp -o /tmp/cvt && /tmp/cvt
//
// The CONFIG screen walks two values with one button, so the arithmetic is the
// whole behaviour: a step that never reaches the maximum, a wrap that lands on 0
// instead of the LCD's floor, or a percent/level mapping that does not round
// trip would leave the owner unable to select a value the panel can already set.

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
  printf("-- LCD percent step (floor 5, ceiling 100, step 10)\n");
  checkInt("5 -> 15", cfgStepUp(5, 5, 100, 10), 15);
  checkInt("90 -> 100", cfgStepUp(90, 5, 100, 10), 100);
  checkInt("95 -> 100, not past it", cfgStepUp(95, 5, 100, 10), 100);
  checkInt("100 -> 5, the floor and not 0", cfgStepUp(100, 5, 100, 10), 5);
  checkInt("below the floor reads as the floor", cfgStepUp(3, 5, 100, 10), 5);
  checkInt("above the ceiling reads as the floor", cfgStepUp(200, 5, 100, 10), 5);
  checkInt("a step that fits lands exactly on the ceiling",
           cfgStepUp(80, 5, 100, 20), 100);

  printf("-- every reachable LCD percent is one the owner can pick\n");
  {
    uint8_t v = 5;
    int seen100 = 0;
    for (int i = 0; i < 40; i++) {
      if (v < 5 || v > 100) {
        printf("FAIL LCD value %d left the range\n", (int)v);
        g_failures++;
        break;
      }
      if (v == 100) seen100 = 1;
      v = cfgStepUp(v, 5, 100, 10);
    }
    checkInt("the walk reaches 100", seen100, 1);
  }

  printf("-- LED percent step (floor 0, ceiling 100, step 10)\n");
  checkInt("0 -> 10", cfgStepUp(0, 0, 100, 10), 10);
  checkInt("90 -> 100", cfgStepUp(90, 0, 100, 10), 100);
  checkInt("100 -> 0", cfgStepUp(100, 0, 100, 10), 0);
  checkInt("below the floor reads as the floor", cfgStepUp(250, 0, 100, 10), 0);

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
