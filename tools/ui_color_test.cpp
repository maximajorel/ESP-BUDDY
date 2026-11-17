// Host-side test for the UI accent colour parsing.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/ui_color_test.cpp -o /tmp/uct && /tmp/uct
//
// The panel sends the colour and the device echoes it back; both directions have
// to agree, and a malformed value must be rejected instead of stored.

#include <cstdio>
#include <cstring>

#include "ui_color.h"

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
  uint32_t rgb = 0;

  printf("-- parsing\n");
  check("#7C4DFF", uiColorParse("#7C4DFF", &rgb) && rgb == 0x7C4DFF);
  check("lowercase 7c4dff", uiColorParse("7c4dff", &rgb) && rgb == 0x7C4DFF);
  check("gold default", uiColorParse("#FFD700", &rgb) && rgb == 0xFFD700);
  check("black", uiColorParse("#000000", &rgb) && rgb == 0x000000);
  check("white", uiColorParse("#ffffff", &rgb) && rgb == 0xFFFFFF);
  check("a short value is rejected", !uiColorParse("#FFD70", &rgb));
  check("a long value is rejected", !uiColorParse("#FFD7000", &rgb));
  check("a non hex digit is rejected", !uiColorParse("#GGD700", &rgb));
  check("an empty string is rejected", !uiColorParse("", &rgb));
  check("null is rejected", !uiColorParse(nullptr, &rgb));

  printf("-- formatting\n");
  char hex[8];
  uiColorToHex(0x7C4DFF, hex);
  check("violet round-trips", strcmp(hex, "#7C4DFF") == 0);
  uiColorToHex(0xFFD700, hex);
  check("gold round-trips", strcmp(hex, "#FFD700") == 0);
  uiColorToHex(0x000000, hex);
  check("black pads the zeros", strcmp(hex, "#000000") == 0);

  printf("-- the two directions agree\n");
  uint32_t back = 0;
  uiColorToHex(0x4C1D95, hex);
  check("format then parse", uiColorParse(hex, &back) && back == 0x4C1D95);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
