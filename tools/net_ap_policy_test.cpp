// Host-side test for the access point duty policy.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/net_ap_policy_test.cpp -o /tmp/nap && /tmp/nap
//
// The rule under test is the one that keeps the owner from ever locking himself
// out: the AP is the only door while the home network is unavailable, and it is
// allowed to go quiet only once the station link has proven itself.

#include <cstdio>

#include "net_ap_policy.h"

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
  const uint32_t HOLD = NET_AP_HOLD_MS;

  printf("-- without a station link the AP is the only way in\n");
  check("no network configured at all", netApShouldRun(NET_AP_AUTO, false, 0),
        true);
  check("configured but not connected", netApShouldRun(NET_AP_AUTO, false, 0),
        true);
  check("never connected, clock irrelevant",
        netApShouldRun(NET_AP_AUTO, false, 999999), true);

  printf("-- a fresh station link does not earn the silence yet\n");
  check("just connected", netApShouldRun(NET_AP_AUTO, true, 0), true);
  check("half a second", netApShouldRun(NET_AP_AUTO, true, 500), true);
  check("one millisecond before the hold",
        netApShouldRun(NET_AP_AUTO, true, HOLD - 1), true);
  check("exactly at the hold", netApShouldRun(NET_AP_AUTO, true, HOLD), false);

  printf("-- once the link is proven, the AP goes quiet\n");
  check("a minute connected", netApShouldRun(NET_AP_AUTO, true, 60000), false);
  check("a day connected", netApShouldRun(NET_AP_AUTO, true, 86400000UL),
        false);

  printf("-- the owner's override beats the link\n");
  check("forced on with a proven link",
        netApShouldRun(NET_AP_FORCE_ON, true, 600000), true);
  check("forced on with no link", netApShouldRun(NET_AP_FORCE_ON, false, 0),
        true);
  check("forced off with no link", netApShouldRun(NET_AP_FORCE_OFF, false, 0),
        false);
  check("forced off with a proven link",
        netApShouldRun(NET_AP_FORCE_OFF, true, 600000), false);

  printf("-- the names the console types\n");
  const char *names[] = {netApModeName(NET_AP_AUTO), netApModeName(NET_AP_FORCE_ON),
                         netApModeName(NET_AP_FORCE_OFF)};
  const char *want[] = {"auto", "on", "off"};
  for (int i = 0; i < 3; i++) {
    bool same = names[i][0] == want[i][0] && names[i][1] == want[i][1] &&
                names[i][2] == want[i][2];
    check(want[i], same, true);
  }

  if (g_failures) {
    printf("\n%d FAILED\n", g_failures);
    return 1;
  }
  printf("\nall net ap policy checks passed\n");
  return 0;
}
