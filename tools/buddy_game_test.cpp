// Host-side test for the web game's mood on the face (S7 of web-inicial).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_game_test.cpp -o /tmp/bgt
//   /tmp/bgt
//
// What matters: the browser can only name a mood the device knows, a mood
// always expires so the face comes back to the bridge, and each mood owns the
// channels that make it readable on the glass without carrying the previous
// expression along.

#include <cmath>
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

  buddyGameSet(&o, BUDDY_GAME_SAD, 2000u, 1000u);
  check("S7 elapsed starts at the mood's own clock",
        buddyGameElapsed(&o, 1000u) == 0u &&
            buddyGameElapsed(&o, 2500u) == 1500u);
  buddyGameSet(&o, BUDDY_GAME_SAD, 3000u, 0xFFFFFF00u);
  check("S7 elapsed survives the millis() wraparound",
        buddyGameElapsed(&o, (uint32_t)(0xFFFFFF00u + 200u)) == 200u);

  buddyGameClear(&o);
  check("S7 clearing ends the override at once",
        buddyGameCurrent(&o, 0) == BUDDY_GAME_NONE);
  check("S7 a cleared mood has no elapsed", buddyGameElapsed(&o, 5000u) == 0u);
}

static BuddyPose posed(BuddyGameMood mood, uint32_t elapsedMs = 0u) {
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
  p.gazeX = 1.0f;
  p.gazeY = -1.0f;
  p.turn = -0.25f;
  p.tilt = 2.0f;
  p.happy = true;
  p.closed = true;
  p.blink = true;
  p.surprise = true;
  p.dim = true;
  buddyGameApplyPose(&p, mood, elapsedMs);
  return p;
}

// The "no" beats are an animation, not a pose, so the test samples the same
// elapsed-time table the firmware uses and watches the signs cross.
static void checkSadNoBeats() {
  int prevGx = 0, prevTurn = 0;
  int gxFlips = 0, turnFlips = 0;
  for (uint32_t t = 4400u; t <= 5800u; t += 10u) {
    BuddyPose p = posed(BUDDY_GAME_SAD, t);
    int gx = p.gazeX > 0.01f ? 1 : (p.gazeX < -0.01f ? -1 : 0);
    int turn = p.turn > 0.005f ? 1 : (p.turn < -0.005f ? -1 : 0);
    if (gx && prevGx && gx != prevGx) gxFlips++;
    if (turn && prevTurn && turn != prevTurn) turnFlips++;
    if (gx) prevGx = gx;
    if (turn) prevTurn = turn;
  }
  check("S7 sad says no with the gaze, not a sway", gxFlips >= 2);
  check("S7 sad says no with the head too", turnFlips >= 2);
}

static void checkSadTimeline() {
  BuddyPose start = posed(BUDDY_GAME_SAD, 0u);
  check("S7 sad starts sad, not neutral",
        start.gazeY > 0.0f && start.gazeX < 0.0f);

  float maxLid = 0.0f;
  float minOpen = 1.0f;
  for (uint32_t t = 0u; t <= 8000u; t += 50u) {
    BuddyPose p = posed(BUDDY_GAME_SAD, t);
    float combined = p.lid + (p.lidL > p.lidR ? p.lidL : p.lidR);
    if (combined > maxLid) maxLid = combined;
    if (p.open < minOpen) minOpen = p.open;
  }
  check("S7 sad never collapses the aperture to the blink bar", minOpen >= 0.35f);
  // A full lid is not a sigh on this board: `buddyEyeHalfH` returns a 4 px bar
  // when shut, and above ~0.70 only a glowing thread is left between the eyes.
  check("S7 sad never reaches the line-in-the-middle lid", maxLid < 1.0f);
  check("S7 sad keeps the sigh a thick slit", maxLid <= 0.65f);

  BuddyPose sigh = posed(BUDDY_GAME_SAD, 3000u);
  check("S7 sad sighs with a heavy lid", sigh.lid > 0.45f && sigh.lid < 1.0f);
  BuddyPose afterSigh = posed(BUDDY_GAME_SAD, 4000u);
  check("S7 sad opens again after the sigh", afterSigh.lid < 0.2f);

  checkSadNoBeats();

  BuddyPose rest = posed(BUDDY_GAME_SAD, 8000u);
  check("S7 sad settles back to the resting sad pose after the hold",
        fabsf(rest.lid - start.lid) < 1e-4f &&
            fabsf(rest.gazeX - start.gazeX) < 1e-4f &&
            fabsf(rest.gazeY - start.gazeY) < 1e-4f &&
            fabsf(rest.turn - start.turn) < 1e-4f);
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
  // Sad is the mirror of anger, not a squint: the outer corners drop and the
  // eye stays open. It must not read like focus, whose even lid is 0.18.
  check("S7 sad drops both outer corners to the mirror of anger",
        sad.rotL < 0.0f && sad.rotR > 0.0f &&
            fabsf(sad.rotL + sad.rotR) < 1e-4f &&
            anger.rotL > 0.0f && anger.rotR < 0.0f);
  check("S7 sad is not an even focus squint", sad.lid < 0.18f);
  check("S7 sad barely cuts the eye from the bottom", sad.bottom < 0.2f);
  check("S7 sad looks down and a little aside",
        sad.gazeY > 0.0f && sad.gazeX < 0.0f);
  check("S7 sad is not hot and does not grow",
        sad.hot == 0.0f && sad.grow == 1.0f);
  check("S7 sad opens the eyes", !sad.closed);
  checkSadTimeline();

  BuddyPose none = posed(BUDDY_GAME_NONE);
  check("S7 none leaves the bridge pose untouched", none.lid == 0.55f);

  // Only SAD owns the gaze/turn/tilt channels: every other mood must keep the
  // bridge's values exactly as it did before the sad gesture existed.
  BuddyPose untouched;
  untouched.gazeX = 2.0f;
  untouched.gazeY = 1.5f;
  untouched.turn = 0.4f;
  untouched.tilt = 3.0f;
  buddyGameApplyPose(&untouched, BUDDY_GAME_FOCUS);
  check("S7 focus does not touch the gaze or the head turn",
        untouched.gazeX == 2.0f && untouched.gazeY == 1.5f &&
            untouched.turn == 0.4f && untouched.tilt == 3.0f);
}

int main() {
  testNames();
  testHold();
  testPoses();

  printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
