// Host-side test for the "no bridge" badge decision (S1 of the link badge).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_badge_test.cpp -o /tmp/bbt && /tmp/bbt
//
// The glass has one badge for "there is no bridge to show". It must appear for
// every way the link can fail, and never while the bridge is answering: an icon
// where the eyes belong is a promise the owner reads as "this is broken".

#include <cstdio>
#include <cstring>

#include "buddy_badge.h"

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
  printf("-- a healthy bridge shows the eyes\n");
  check("ok shows the face", !buddyShowsLinkBadge(BUDDY_LINK_OK));

  printf("-- every failure shows the badge\n");
  check("offline", buddyShowsLinkBadge(BUDDY_LINK_OFFLINE));
  check("rejected certificate", buddyShowsLinkBadge(BUDDY_LINK_TLS));
  check("cloudflare block", buddyShowsLinkBadge(BUDDY_LINK_BLOCKED));
  check("wrong token", buddyShowsLinkBadge(BUDDY_LINK_UNAUTHORIZED));
  check("bridge error status", buddyShowsLinkBadge(BUDDY_LINK_HTTP));
  check("no synced clock", buddyShowsLinkBadge(BUDDY_LINK_NO_TIME));
  check("stored URL unusable", buddyShowsLinkBadge(BUDDY_LINK_BAD_URL));

  printf("-- never configured keeps the quiet face\n");
  // The dim, calm face is designed for "nobody paired me yet"; an alarm icon
  // there would read as damage instead of a pending setup.
  check("unconfigured keeps the face",
        !buddyShowsLinkBadge(BUDDY_LINK_UNCONFIGURED));

  printf("-- the decision covers every cause exactly once\n");
  int badges = 0;
  for (int i = 0; i < BUDDY_LINK_COUNT; i++) {
    if (buddyShowsLinkBadge((BuddyLink)i)) badges++;
  }
  // Two of the nine causes keep the face: ok and unconfigured.
  check("7 of 9 causes show the badge", badges == 7);
  check("the only face is ok", !buddyShowsLinkBadge((BuddyLink)0));
  check("out-of-range cause is not a badge",
        !buddyShowsLinkBadge((BuddyLink)99));

  if (g_failures) {
    printf("\n%d failure(s)\n", g_failures);
    return 1;
  }
  printf("\nall ok\n");
  return 0;
}
