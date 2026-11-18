// Host-side test for the local sleep clock.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_sleep_test.cpp -o /tmp/bst && /tmp/bst
//
// The server decides the state; the board decides patience. `sleep_hint` is an
// input, never an order: the MIN of the server's silence and the board's own
// silence is what puts the face to sleep, so a busy server cannot be dozed
// through and a server that has been quiet for minutes cannot override a shake.

#include <cstdio>

#include "buddy_sleep.h"

static int g_failures = 0;

static void checkInt(const char *what, long got, long want) {
  if (got != want) {
    printf("FAIL %-56s got=%ld want=%ld\n", what, got, want);
    g_failures++;
  } else {
    printf("ok   %-56s %ld\n", what, got);
  }
}

// hintValid=false is the bridge that does not speak the field yet.
static BuddySleepStage stage(float hintSec, bool hintValid, uint32_t localIdleMs) {
  BuddySleepFacts f;
  f.hintSec = hintSec;
  f.hintValid = hintValid;
  f.localIdleMs = localIdleMs;
  return buddySleepStage(f);
}

int main() {
  printf("-- the thresholds are the published ones\n");
  checkInt("light at 60 s", BUDDY_SLEEP_LIGHT_SEC, 60);
  checkInt("deep at 180 s", BUDDY_SLEEP_DEEP_SEC, 180);

  printf("-- local silence decides when the server says nothing\n");
  checkInt("1 s of silence stays awake", stage(0, false, 1000),
           BUDDY_SLEEP_AWAKE);
  checkInt("59.999 s stays awake", stage(0, false, 59999), BUDDY_SLEEP_AWAKE);
  checkInt("exactly 60 s is light", stage(0, false, 60000), BUDDY_SLEEP_LIGHT);
  checkInt("119 s is still light", stage(0, false, 119000), BUDDY_SLEEP_LIGHT);
  checkInt("exactly 180 s is deep", stage(0, false, 180000), BUDDY_SLEEP_DEEP);
  checkInt("an hour asleep is deep", stage(0, false, 3600000), BUDDY_SLEEP_DEEP);

  printf("-- the server being busy keeps the face awake\n");
  checkInt("hint 0 while the server works", stage(0.0f, true, 400000),
           BUDDY_SLEEP_AWAKE);
  checkInt("hint 4.9 is still the server working", stage(4.9f, true, 400000),
           BUDDY_SLEEP_AWAKE);
  checkInt("hint 5.0 is only five seconds of silence, so still awake",
           stage(5.0f, true, 400000), BUDDY_SLEEP_AWAKE);
  checkInt("hint 60 caps a deep local silence at light", stage(60.0f, true, 400000),
           BUDDY_SLEEP_LIGHT);
  checkInt("hint 180 lets a deep local silence be deep",
           stage(180.0f, true, 400000), BUDDY_SLEEP_DEEP);

  printf("-- a shake outranks a high hint (the bug this fixes)\n");
  checkInt("moved 10 s ago, server quiet 300 s", stage(300.0f, true, 10000),
           BUDDY_SLEEP_AWAKE);
  checkInt("moved 59 s ago, server quiet 300 s", stage(300.0f, true, 59000),
           BUDDY_SLEEP_AWAKE);
  checkInt("moved 61 s ago, server quiet 300 s", stage(300.0f, true, 61000),
           BUDDY_SLEEP_LIGHT);
  checkInt("moved 181 s ago, server quiet 300 s", stage(300.0f, true, 181000),
           BUDDY_SLEEP_DEEP);

  printf("-- junk from the bridge never sleeps or panics\n");
  checkInt("a negative hint reads as the server working",
           stage(-5.0f, true, 400000), BUDDY_SLEEP_AWAKE);
  checkInt("an absurd hint is clamped, not trusted",
           stage(1e9f, true, 400000), BUDDY_SLEEP_DEEP);
  checkInt("a missing hint falls back to local silence",
           stage(0.0f, false, 400000), BUDDY_SLEEP_DEEP);

  printf("-- a stimulus zeroes the local silence at once\n");
  BuddySleepClock clock = {};
  checkInt("boot counts as a stimulus, so the first idle is measured from 0",
           buddySleepClockStage(&clock, 900000, 0.0f, false), BUDDY_SLEEP_DEEP);
  buddySleepStimulus(&clock, 900000);
  checkInt("right after the shake", buddySleepClockStage(&clock, 900000, 0.0f, false),
           BUDDY_SLEEP_AWAKE);
  checkInt("a second after the shake",
           buddySleepClockStage(&clock, 901000, 0.0f, false), BUDDY_SLEEP_AWAKE);
  checkInt("a minute after the shake, with the server still quiet",
           buddySleepClockStage(&clock, 961000, 300.0f, true), BUDDY_SLEEP_LIGHT);
  checkInt("and a high hint does not push it deeper than its own silence",
           buddySleepClockStage(&clock, 961000, 0.0f, false), BUDDY_SLEEP_LIGHT);

  printf("-- the clock guards the millis() wrap and a future stamp\n");
  BuddySleepClock wrapped = {};
  wrapped.lastStimulusMs = 0xFFFFFF00u;
  checkInt("the wrap is not a 49-day sleep", buddySleepClockIdleMs(&wrapped, 10), 266);
  checkInt("and it keeps counting after the wrap",
           buddySleepClockStage(&wrapped, 300000, 0.0f, false), BUDDY_SLEEP_DEEP);
  BuddySleepClock ahead = {};
  ahead.lastStimulusMs = 5000;
  checkInt("a stamp ahead of the clock reads as just moved",
           buddySleepClockIdleMs(&ahead, 4000), 0);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
