// Host-side test for the web game's mood on the face (S7 of web-inicial).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_game_test.cpp -o /tmp/bgt
//   /tmp/bgt
//
// What matters: the browser can only name a mood the device knows, a mood
// always expires so the face comes back to the bridge, and each mood owns the
// channels that make it readable on the glass without carrying the previous
// expression along.

#include <cstdio>
#include <cstring>

#include "buddy_game.h"

static int g_failures = 0;
static int g_checks = 0;

static void check(const char *what, bool ok) {
  g_checks++;
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void testNames() {
  check("S7 focus parses",
        buddyGameMoodFromName("focus", BUDDY_GAME_NONE) == BUDDY_GAME_FOCUS);
  check("S7 thinking parses",
        buddyGameMoodFromName("thinking", BUDDY_GAME_NONE) == BUDDY_GAME_THINKING);
  check("S7 alert parses",
        buddyGameMoodFromName("alert", BUDDY_GAME_NONE) == BUDDY_GAME_ALERT);
  check("S7 happy parses",
        buddyGameMoodFromName("happy", BUDDY_GAME_NONE) == BUDDY_GAME_HAPPY);
  check("S7 anger parses",
        buddyGameMoodFromName("anger", BUDDY_GAME_NONE) == BUDDY_GAME_ANGER);
  check("S7 sleepy parses",
        buddyGameMoodFromName("sleepy", BUDDY_GAME_NONE) == BUDDY_GAME_SLEEPY);
  check("S7 sad parses",
        buddyGameMoodFromName("sad", BUDDY_GAME_NONE) == BUDDY_GAME_SAD);
  check("S7 the empty name clears",
        buddyGameMoodFromName("", BUDDY_GAME_ANGER) == BUDDY_GAME_NONE);
  check("S7 a null name clears",
        buddyGameMoodFromName(nullptr, BUDDY_GAME_ANGER) == BUDDY_GAME_NONE);
  check("S7 an unknown name clears",
        buddyGameMoodFromName("excited", BUDDY_GAME_ANGER) == BUDDY_GAME_NONE);
  check("S7 the case is exact",
        buddyGameMoodFromName("Anger", BUDDY_GAME_ANGER) == BUDDY_GAME_NONE);

  check("S7 wire names round-trip",
        buddyGameMoodFromName(buddyGameMoodWireName(BUDDY_GAME_THINKING),
                              BUDDY_GAME_NONE) == BUDDY_GAME_THINKING);
  check("S7 none is its own wire name",
        strcmp(buddyGameMoodWireName(BUDDY_GAME_NONE), "") == 0);
}

static void testHold() {
  check("S7 no hold reads the default",
        buddyGameClampMs(0) == BUDDY_GAME_DEFAULT_MS);
  check("S7 a short hold is raised to the minimum",
        buddyGameClampMs(1) == BUDDY_GAME_MIN_MS);
  check("S7 a long hold is capped",
        buddyGameClampMs(60000u) == BUDDY_GAME_MAX_MS);
  check("S7 a sane hold passes through",
        buddyGameClampMs(2500u) == 2500u);

  BuddyGameOverride o;
  buddyGameSet(&o, BUDDY_GAME_ANGER, 2000u, 1000u);
  check("S7 the mood is active inside its window",
        buddyGameActive(&o, 1500u));
  check("S7 the mood is spent exactly at its deadline",
        !buddyGameActive(&o, 3000u));
  check("S7 a spent override reads as none",
        buddyGameCurrent(&o, 3500u) == BUDDY_GAME_NONE);
  check("S7 an active override reports its mood",
        buddyGameCurrent(&o, 2000u) == BUDDY_GAME_ANGER);

  // The deadline subtraction is wrap-safe: a millis() rollover must not extend
  // or cancel a game reaction.
  buddyGameSet(&o, BUDDY_GAME_FOCUS, 3000u, 0xFFFFFE00u);
  check("S7 a hold survives the millis() wraparound",
        buddyGameActive(&o, (uint32_t)(0xFFFFFE00u + 1000u)));
  check("S7 a hold still expires after the wraparound",
        !buddyGameActive(&o, (uint32_t)(0xFFFFFE00u + 3001u)));

  buddyGameClear(&o);
  check("S7 clearing ends the override at once",
        buddyGameCurrent(&o, 0) == BUDDY_GAME_NONE);
}

static BuddyPose posed(BuddyGameMood mood) {
  BuddyPose p;
  // Start from a face the bridge could plausibly be showing: narrowed lids and
  // the previous expression's channels set, so every mood has to clear what it
  // does not own.
  p.lid = 0.55f;
  p.lidL = 0.3f;
  p.lidR = 0.3f;
  p.bottom = 0.4f;
  p.hot = 0.5f;
  p.grow = 1.04f;
  p.rotL = 3.0f;
  p.rotR = -3.0f;
  p.happy = true;
  p.closed = true;
  p.blink = true;
  p.surprise = true;
  p.dim = true;
  buddyGameApplyPose(&p, mood);
  return p;
}

static void testPoses() {
  BuddyPose focus = posed(BUDDY_GAME_FOCUS);
  check("S7 every mood clears the unpaired dim", !focus.dim);
  check("S7 focus narrows the lids", focus.lid > 0.15f && focus.lid < 0.25f);
  check("S7 focus opens the eyes", !focus.closed && !focus.blink);
  check("S7 focus drops the hot tint", focus.hot == 0.0f);
  check("S7 focus is not happy", !focus.happy);
  check("S7 focus drops the asymmetric lids", focus.lidL == 0.0f && focus.lidR == 0.0f);

  BuddyPose thinking = posed(BUDDY_GAME_THINKING);
  check("S7 thinking is asymmetric",
        thinking.lidR > thinking.lidL + 0.2f && thinking.lidL < 0.1f);
  check("S7 thinking is not closed", !thinking.closed);

  BuddyPose alert = posed(BUDDY_GAME_ALERT);
  check("S7 alert opens the eyes wide", alert.lid < 0.05f);
  check("S7 alert is not closed", !alert.closed);

  BuddyPose happy = posed(BUDDY_GAME_HAPPY);
  check("S7 happy sets the joy channel", happy.happy);
  check("S7 happy opens the eyes", !happy.closed && happy.lid < 0.05f);

  BuddyPose anger = posed(BUDDY_GAME_ANGER);
  check("S7 anger turns the eyes hot", anger.hot > 0.9f);
  check("S7 anger grows the eye", anger.grow > 1.05f);
  check("S7 anger angles the lids apart",
        anger.rotL > 0.0f && anger.rotR < 0.0f);
  check("S7 anger opens the eyes", !anger.closed && !anger.happy);

  BuddyPose sleepy = posed(BUDDY_GAME_SLEEPY);
  check("S7 sleepy shuts the eyes", sleepy.closed && sleepy.lid > 0.9f);

  BuddyPose sad = posed(BUDDY_GAME_SAD);
  check("S7 sad cuts the eye from the bottom", sad.bottom > 0.3f);
  check("S7 sad is not hot", sad.hot == 0.0f);
  check("S7 sad opens the eyes", !sad.closed);

  BuddyPose none = posed(BUDDY_GAME_NONE);
  check("S7 none leaves the bridge pose untouched", none.lid == 0.55f);
}

int main() {
  testNames();
  testHold();
  testPoses();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
