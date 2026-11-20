// Host-side test for the bead's steady hold layer.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/rgb_hold_test.cpp -o /tmp/rht && /tmp/rht
//
// The hold exists so a finished study session can paint a steady colour over the
// buddy's per-loop base colour. It has to expire exactly once, clamp absurd
// durations, and survive the 49.7-day millis() wrap.

#include <cstdio>

#include "rgb_hold.h"

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
  printf("-- starting\n");
  {
    RgbHold h;
    check("a fresh hold is off", !h.active);
    check("a fresh hold does not own the pixel", !rgbHoldStep(&h, 0));
    rgbHoldStart(&h, 1000, 0, 200, 60, 2500);
    check("starting makes it active", h.active);
    check("it keeps the colour", h.r == 0 && h.g == 200 && h.b == 60);
    check("it owns the pixel right away", rgbHoldStep(&h, 1000));
    check("it still owns it before the end", rgbHoldStep(&h, 3499));
  }

  printf("-- expiring\n");
  {
    RgbHold h;
    rgbHoldStart(&h, 1000, 255, 0, 0, 2500);
    check("the deadline itself is over", !rgbHoldStep(&h, 3500));
    check("after expiring it is idle", !h.active);
    check("expiring twice is not a new hold", !rgbHoldStep(&h, 3600));
  }

  printf("-- clearing and zero\n");
  {
    RgbHold h;
    rgbHoldStart(&h, 0, 10, 20, 30, 5000);
    rgbHoldClear(&h);
    check("clearing turns it off", !h.active);
    check("a cleared hold does not own the pixel", !rgbHoldStep(&h, 10));
    rgbHoldStart(&h, 0, 10, 20, 30, 0);
    check("zero milliseconds is a no-op, not a hold", !h.active);
  }

  printf("-- bounds\n");
  {
    RgbHold h;
    rgbHoldStart(&h, 0, 1, 2, 3, 10u * 60u * 1000u);
    check("an absurd duration clamps", rgbHoldStep(&h, RGB_HOLD_MS_MAX - 1));
    check("the clamp still expires", !rgbHoldStep(&h, RGB_HOLD_MS_MAX));
  }

  printf("-- the millis wrap\n");
  {
    RgbHold h;
    uint32_t start = 0xFFFFFF00u;
    rgbHoldStart(&h, start, 0, 200, 60, 2500);
    check("a hold across the wrap is still on", rgbHoldStep(&h, start + 1000u));
    check("a hold across the wrap still ends", !rgbHoldStep(&h, start + 2500u));
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall rgb_hold checks passed\n");
  return 0;
}
