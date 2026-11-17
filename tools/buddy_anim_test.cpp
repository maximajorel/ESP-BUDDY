// Host-side test for the eye pose ladders (Sofía's brief, tasks 3 and 4 of the
// firmware list).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_anim_test.cpp -o /tmp/bat && /tmp/bat
//
// Pins the duration ladders: bored escalates, long thinking never sleeps, tool
// entry surprises, error crosses the eyes, done is happy for a second, and a
// one-poll state flicker is held back.

#include <cstdio>

#include "buddy_anim.h"

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
  const uint32_t seed = 1234;

  printf("-- anti-flicker hold\n");
  check("a state younger than the hold is not shown",
        buddyShownState(BUDDY_IDLE, BUDDY_TOOL, 100) == BUDDY_IDLE);
  check("a state that survives the hold is shown",
        buddyShownState(BUDDY_IDLE, BUDDY_TOOL, 300) == BUDDY_TOOL);
  check("the same state is always shown",
        buddyShownState(BUDDY_IDLE, BUDDY_IDLE, 0) == BUDDY_IDLE);

  printf("-- bored ladder\n");
  BuddyPose p = buddyPose(BUDDY_BORED, 1000, 1000, seed);
  check("0-20 s is neutral and open", !p.closed && p.lid < 0.02f);
  p = buddyPose(BUDDY_BORED, 30000, 30000, seed);
  check("20-60 s drifts the gaze", p.gazeX != 0.0f || p.lid > 0.05f);
  p = buddyPose(BUDDY_BORED, 70000, 70000, seed);
  check("60-120 s drops the lids", p.lid >= 0.3f && !p.closed);
  p = buddyPose(BUDDY_BORED, 130000, 130000, seed);
  check("120 s+ is asleep", p.closed);

  printf("-- gesture rotation\n");
  BuddyGesture g0 = buddyGestureAt(0, seed);
  check("starts on the first gesture", g0.slot == 0 && g0.startMs == 0);
  check("a gesture lasts at least 3 s", buddyGestureAt(3000, seed).slot == 0);
  check("and at most 6 s", buddyGestureAt(6200, seed).slot >= 1);

  printf("-- thinking cycle\n");
  p = buddyPose(BUDDY_THINKING, 1000, 1000, seed);
  check("first gesture looks up-left", p.gazeY <= -8.0f);
  check("first gesture tilts the eyes", p.rot != 0.0f);
  check("first gesture squints one eye", p.lidR > p.lidL);
  int seenThinking[5] = {0};
  for (uint32_t age = 0; age < 20000; age += 100) {
    BuddyGesture g = buddyGestureAt(age, seed);
    seenThinking[g.slot % 5] = 1;
  }
  check("the cycle uses all five thinking gestures",
        seenThinking[0] && seenThinking[1] && seenThinking[2] &&
        seenThinking[3] && seenThinking[4]);
  bool tired = false;
  bool asleep = false;
  for (uint32_t age = 20000; age < 40000; age += 100) {
    BuddyGesture g = buddyGestureAt(age, seed);
    BuddyPose q = buddyPose(BUDDY_THINKING, age, age, seed);
    if (q.closed) asleep = true;
    if (g.slot % 6 == 5 && q.bottom > 0.2f) tired = true;
  }
  check("20 s+ adds the tired bottom-lid gesture", tired);
  check("thinking never falls asleep", !asleep);

  printf("-- tool cycle\n");
  p = buddyPose(BUDDY_TOOL, 50, 50, seed);
  check("tool entry surprises (wide eyes)", p.surprise && p.rx > 50);
  int seenTool[4] = {0};
  bool doubleBlink = false;
  for (uint32_t age = 200; age < 30000; age += 50) {
    BuddyGesture g = buddyGestureAt(age, seed ^ 0x51ed270bu);
    uint32_t which = g.slot % 4;
    seenTool[which] = 1;
    if (which == 3 && buddyPose(BUDDY_TOOL, age, age, seed).blink) {
      doubleBlink = true;
    }
  }
  check("the tool cycle uses reading, focus, tension and double blink",
        seenTool[0] && seenTool[1] && seenTool[2] && seenTool[3]);
  check("the double blink fires in its gesture", doubleBlink);
  bool sigh = false;
  for (uint32_t age = 30000; age < 45000; age += 100) {
    BuddyGesture g = buddyGestureAt(age, seed ^ 0x51ed270bu);
    if (g.slot % 5 != 4) continue;
    BuddyPose q = buddyPose(BUDDY_TOOL, age, age, seed);
    if (q.lid >= 0.15f && q.gazeY > 0) sigh = true;
  }
  check("30 s+ adds the sigh gesture", sigh);

  printf("-- tool, error, done, offline\n");
  p = buddyPose(BUDDY_TOOL, 50, 50, seed);
  check("tool entry surprises (wide eyes)", p.surprise && p.rx > 50);
  p = buddyPose(BUDDY_ERROR, 1000, 1000, seed);
  check("error crosses the eyes", p.eyeGazeL > 0 && p.eyeGazeR < 0);
  p = buddyPose(BUDDY_DONE, 100, 100, seed);
  check("done is happy for the first second", p.happy);
  p = buddyPose(BUDDY_DONE, 2000, 2000, seed);
  check("done relaxes afterwards", !p.happy && p.lid > 0.0f);
  p = buddyPose(BUDDY_OFFLINE, 1000, 1000, seed);
  check("offline is dim", p.dim);
  p = buddyPose(BUDDY_UNCONFIGURED, 1000, 1000, seed);
  check("unconfigured is dim", p.dim);

  printf("-- blink rhythm\n");
  int blinks = 0;
  int frames = 0;
  for (uint32_t t = 0; t < 20000; t += 50) {
    if (buddyPose(BUDDY_IDLE, 1000, t, seed).blink) blinks++;
    frames++;
  }
  check("the idle face blinks", blinks > 0);
  check("but not constantly", blinks * 100 < frames * 30);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
