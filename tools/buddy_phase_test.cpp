// Host-side test for the bridge phase mapping (Sofía's second doc, HUD section).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_phase_test.cpp -o /tmp/bpt && /tmp/bpt
//
// The phase is authoritative: unknown or missing must be idle (never a false
// check), and the default LED colours must match the published table.

#include <cstdio>
#include <cstring>

#include "buddy_phase.h"

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
  printf("-- phase names\n");
  check("busy", buddyPhaseFromName("busy") == BUDDY_PHASE_BUSY);
  check("ok", buddyPhaseFromName("ok") == BUDDY_PHASE_OK);
  check("ask", buddyPhaseFromName("ask") == BUDDY_PHASE_ASK);
  check("idle", buddyPhaseFromName("idle") == BUDDY_PHASE_IDLE);
  check("unknown is idle, not ok", buddyPhaseFromName("dancing") == BUDDY_PHASE_IDLE);
  check("missing is idle", buddyPhaseFromName(nullptr) == BUDDY_PHASE_IDLE);
  check("empty is idle", buddyPhaseFromName("") == BUDDY_PHASE_IDLE);

  printf("-- wire names round-trip\n");
  check("busy", strcmp(buddyPhaseWireName(BUDDY_PHASE_BUSY), "busy") == 0);
  check("ok", strcmp(buddyPhaseWireName(BUDDY_PHASE_OK), "ok") == 0);
  check("ask", strcmp(buddyPhaseWireName(BUDDY_PHASE_ASK), "ask") == 0);
  check("idle", strcmp(buddyPhaseWireName(BUDDY_PHASE_IDLE), "idle") == 0);

  printf("-- busy -> idle is a finish (Desktop/Telegram)\n");
  check("ok always finishes", buddyPhaseFinished(BUDDY_PHASE_BUSY, BUDDY_PHASE_OK));
  check("busy -> idle finishes", buddyPhaseFinished(BUDDY_PHASE_BUSY, BUDDY_PHASE_IDLE));
  check("idle -> busy does not", !buddyPhaseFinished(BUDDY_PHASE_IDLE, BUDDY_PHASE_BUSY));
  check("ask does not finish", !buddyPhaseFinished(BUDDY_PHASE_BUSY, BUDDY_PHASE_ASK));
  check("idle -> idle does not", !buddyPhaseFinished(BUDDY_PHASE_IDLE, BUDDY_PHASE_IDLE));

  printf("-- default LED colours\n");
  BuddyRgb c = buddyPhaseDefaultLed(BUDDY_PHASE_BUSY);
  check("busy yellow", c.r == 255 && c.g == 190 && c.b == 0);
  c = buddyPhaseDefaultLed(BUDDY_PHASE_OK);
  check("ok green", c.r == 0 && c.g == 255 && c.b == 80);
  c = buddyPhaseDefaultLed(BUDDY_PHASE_ASK);
  check("ask red", c.r == 255 && c.g == 40 && c.b == 40);
  c = buddyPhaseDefaultLed(BUDDY_PHASE_IDLE);
  check("idle cyan", c.r == 0 && c.g == 180 && c.b == 255);
  c = buddyEyesCyan();
  check("eyes and idle LED share cyan", c.r == 0 && c.g == 180 && c.b == 255);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
