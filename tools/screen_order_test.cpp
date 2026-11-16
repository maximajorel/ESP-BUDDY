// Host-side test for the screen order validation.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/screen_order_test.cpp -o /tmp/sot && /tmp/sot
//
// The order decides which screen the picker reaches next. A duplicate would make
// one screen unreachable and show another twice, so the rule is pinned here
// instead of trusted.

#include <cstdio>
#include <cstring>

#include "screen_order.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-52s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-52s %d\n", what, (int)got);
  }
}

int main() {
  const uint8_t identity[5] = {0, 1, 2, 3, 4};
  check("identity order is valid", screenOrderValid(identity, 5), true);

  const uint8_t reordered[5] = {2, 0, 4, 1, 3};
  check("any permutation is valid", screenOrderValid(reordered, 5), true);

  const uint8_t duplicate[5] = {0, 1, 1, 3, 4};
  check("a duplicate is rejected", screenOrderValid(duplicate, 5), false);

  const uint8_t missing[5] = {0, 1, 2, 3, 3};
  check("a missing screen is rejected", screenOrderValid(missing, 5), false);

  const uint8_t outOfRange[5] = {0, 1, 2, 3, 9};
  check("an out-of-range slot is rejected", screenOrderValid(outOfRange, 5), false);

  const uint8_t shortOrder[4] = {0, 1, 2, 3};
  check("a short order is read as out of range", screenOrderValid(shortOrder, 5), false);

  check("a null order is rejected", screenOrderValid(nullptr, 5), false);
  check("a zero count is rejected", screenOrderValid(identity, 0), false);

  const uint8_t one[1] = {0};
  check("a single-screen device is valid", screenOrderValid(one, 1), true);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
  return g_failures == 0 ? 0 : 1;
}
