// Host-side test for Sofía's dance (task 1 of `odd/tasks/baile-spotify.md`).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_dance_test.cpp -o /tmp/bdt && /tmp/bdt
//
// Pins the four things the renderer and the trigger rely on: the BPM a track
// gets, the beat/bar arithmetic that follows the song's own position, the pose
// the rhythm paints (and that it clears the channels it does not use), and the
// life of a note so one can never wander onto an eye.

#include <cstdio>

#include "buddy_dance.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void checkNear(const char *what, float got, float want, float eps) {
  bool ok = fabsf(got - want) <= eps;
  if (!ok) printf("  got %.4f want %.4f\n", got, want);
  check(what, ok);
}

int main() {
  printf("-- tempo per track\n");
  const uint32_t bpmA = buddyDanceBpm("4uLU6hMCjMI75M1A2tKUQC");
  const uint32_t bpmB = buddyDanceBpm("4uLU6hMCjMI75M1A2tKUQC");
  check("the same track always dances at the same tempo", bpmA == bpmB);
  check("the tempo sits in the range a body bobs to",
        bpmA >= BUDDY_DANCE_BPM_MIN && bpmA <= BUDDY_DANCE_BPM_MAX);
  check("no track id is still a valid tempo",
        buddyDanceBpm(nullptr) >= BUDDY_DANCE_BPM_MIN &&
            buddyDanceBpm("") >= BUDDY_DANCE_BPM_MIN);
  int distinct = 0;
  bool seen[BUDDY_DANCE_BPM_MAX + 1] = {false};
  for (int i = 0; i < 40; i++) {
    char id[16];
    snprintf(id, sizeof(id), "track%02d", i);
    uint32_t bpm = buddyDanceBpm(id);
    check("a generated id stays in range",
          bpm >= BUDDY_DANCE_BPM_MIN && bpm <= BUDDY_DANCE_BPM_MAX);
    if (!seen[bpm]) {
      seen[bpm] = true;
      distinct++;
    }
  }
  check("different tracks do not all share one tempo", distinct >= 10);

  printf("-- the beat follows the song position\n");
  BuddyDanceBeat b = buddyDanceBeatAt(120, 0);
  check("the start of a track is the downbeat",
        b.beatIndex == 0 && b.barIndex == 0 && b.beatPhase == 0.0f &&
            b.barPhase == 0.0f);
  b = buddyDanceBeatAt(120, 250);
  checkNear("half a beat at 120 bpm is a half phase", b.beatPhase, 0.5f, 0.001f);
  check("half a beat is still the first beat", b.beatIndex == 0);
  b = buddyDanceBeatAt(120, 500);
  check("a beat later the index advances and the phase restarts",
        b.beatIndex == 1 && b.beatPhase == 0.0f);
  b = buddyDanceBeatAt(120, 2250);
  check("four and a half beats is bar one, phase 1/8",
        b.barIndex == 1 && b.beatIndex == 4);
  checkNear("the bar phase covers the whole bar", b.barPhase, 0.125f, 0.001f);
  b = buddyDanceBeatAt(10, 1000);
  check("a nonsense tempo clamps to the floor", b.bpm == BUDDY_DANCE_BPM_MIN);
  b = buddyDanceBeatAt(400, 1000);
  check("a nonsense tempo clamps to the ceiling", b.bpm == BUDDY_DANCE_BPM_MAX);

  printf("-- the pose the rhythm paints\n");
  BuddyPose p;
  p.hot = 0.8f;
  p.happy = true;
  p.closed = true;
  p.surprise = true;
  p.lid = 0.9f;
  p.lidL = 0.4f;
  p.tremble = 0.7f;
  p.grow = 1.4f;
  p.glow = 0.2f;
  p.eyeGazeL = 6.0f;
  buddyDancePose(buddyDanceBeatAt(120, 1000), 1000, &p);
  check("the dance takes the face back to its own channels",
        !p.happy && !p.closed && !p.surprise && p.hot == 0.0f &&
            p.lidL == 0.0f && p.tremble == 0.0f && p.glow == 1.0f &&
            p.eyeGazeL == 0.0f);
  check("the eye keeps the neutral squircle",
        p.rx == 48 && p.ry == 48 && p.grow >= 1.0f && p.grow <= 1.04f);
  check("the downbeat dips the lid", p.lid > 0.06f && p.lid <= 0.16f);
  check("the downbeat lifts the head", p.bob < 0.0f);

  BuddyPose late;
  buddyDancePose(buddyDanceBeatAt(120, 490), 490, &late);
  check("just before the next beat the kick is spent", fabsf(late.bob) < 0.2f);
  check("the lid comes back up between beats", late.lid < 0.07f);

  // A bar at 120 bpm is 2 s: beat 1 sits a quarter in, beat 3 three quarters in.
  BuddyPose right;
  buddyDancePose(buddyDanceBeatAt(120, 500), 500, &right);
  BuddyPose left;
  buddyDancePose(buddyDanceBeatAt(120, 1500), 1500, &left);
  checkNear("a quarter into the bar the eyes go with the sway", right.gazeX,
            7.0f, 0.5f);
  checkNear("three quarters in they are on the other side", left.gazeX, -3.0f,
            0.5f);
  check("the head turns with the gaze", right.turn > 0.0f && left.turn < 0.0f);
  check("the head tips against the sway", right.tilt < 0.0f && left.tilt > 0.0f);

  BuddyPose blink;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 7 + 20), 4000, &blink);
  check("the dance blinks on its own count", blink.blink);
  BuddyPose noBlink;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 9 + 20), 5000, &noBlink);
  check("and stays open off the count", !noBlink.blink);

  printf("-- the music truth survives the poll cycle\n");
  // The bug this pins: spotify.cpp sets LOADING at the start of every poll (every
  // 3 s), so a gate on the raw feed stopped and restarted the dance forever.
  BuddyDanceLatch latch;
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_UNKNOWN, 0);
  check("with no answer yet the face does not dance",
        !buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_PLAYING, 1000);
  check("a playing track starts the dance", buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_UNKNOWN, 3000);
  check("a poll in flight does not stop it", buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_UNKNOWN, 3000 + 6000);
  check("neither does a whole cycle of them", buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_PLAYING, 3000 + 6000);
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_UNKNOWN, 3000 + 6000 + 12000);
  check("a long gap inside the window still dances",
        buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_UNKNOWN,
                      3000 + 6000 + BUDDY_DANCE_HOLD_MS + 1);
  check("past the window an old answer expires", !buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_PLAYING, 40000);
  check("the music comes back and so does the dance",
        buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_SILENT, 41000);
  check("a 204 stops it at once", !buddyDanceLatchActive(&latch));
  buddyDanceLatchStep(&latch, BUDDY_DANCE_MUSIC_UNKNOWN, 42000);
  check("and an old silence is not turned back into a dance",
        !buddyDanceLatchActive(&latch));

  printf("-- the notes\n");
  checkNear("a note is invisible the instant it is born",
            buddyDanceNoteAlpha(0.0f), 0.0f, 0.001f);
  checkNear("a note is solid in its hold", buddyDanceNoteAlpha(0.30f), 1.0f,
            0.001f);
  check("a note is gone at the end of its life",
        buddyDanceNoteAlpha(1.0f) == 0.0f && buddyDanceNoteAlpha(1.4f) == 0.0f);

  BuddyNoteField field;
  BuddyDanceBeat b0 = buddyDanceBeatAt(120, 0);
  buddyDanceNotesStep(&field, b0, 0, 0);
  check("the beat already playing at the start spawns nothing",
        !field.notes[0].active);
  buddyDanceNotesStep(&field, buddyDanceBeatAt(120, 500), 500, 500);
  check("an odd beat spawns nothing either", !field.notes[0].active);
  buddyDanceNotesStep(&field, buddyDanceBeatAt(120, 1000), 1000, 500);
  check("the second beat spawns the first note", field.notes[0].active);
  check("the first note is born in the left field",
        field.notes[0].x >= BUDDY_NOTE_LEFT_MIN &&
            field.notes[0].x <= BUDDY_NOTE_LEFT_MAX);
  check("it is born at the bottom of the glass",
        fabsf(field.notes[0].y - BUDDY_NOTE_Y0) < 1.0f);
  buddyDanceNotesStep(&field, buddyDanceBeatAt(120, 2000), 2000, 500);
  check("the next due beat answers on the right",
        field.notes[1].active && field.notes[1].x >= BUDDY_NOTE_RIGHT_MIN &&
            field.notes[1].x <= BUDDY_NOTE_RIGHT_MAX);
  check("the first note has climbed",
        field.notes[0].y < BUDDY_NOTE_Y0 - 20.0f);
  check("and it is visible while it climbs", field.notes[0].alpha > 0.5f);
  // A beat that does not spawn, so the only thing that changes is the expiry.
  buddyDanceNotesStep(&field, buddyDanceBeatAt(120, 3700), 3700, 100);
  check("a note dies at the end of its life", !field.notes[0].active);
  bool inside = true;
  for (int i = 0; i < BUDDY_NOTE_COUNT; i++) {
    if (!field.notes[i].active) continue;
    if (field.notes[i].x < field.notes[i].minX ||
        field.notes[i].x > field.notes[i].maxX) {
      inside = false;
    }
  }
  check("no note ever leaves its side field", inside);
  buddyDanceNotesClear(&field);
  check("clearing wipes the field", !field.notes[0].active && !field.primed);

  printf("-- the field survives a long stall\n");
  BuddyNoteField stalled;
  buddyDanceNotesStep(&stalled, buddyDanceBeatAt(120, 0), 0, 0);
  for (int i = 1; i <= 200; i++) {
    buddyDanceNotesStep(&stalled, buddyDanceBeatAt(120, (uint32_t)i * 500),
                        (uint32_t)i * 500, 500);
  }
  int alive = 0;
  for (int i = 0; i < BUDDY_NOTE_COUNT; i++) {
    if (stalled.notes[i].active) alive++;
  }
  check("the field never overflows", alive <= BUDDY_NOTE_COUNT);
  check("and it keeps notes on the glass while the music plays", alive > 0);

  if (g_failures) {
    printf("\n%d FAILURES\n", g_failures);
    return 1;
  }
  printf("\ntodo verde\n");
  return 0;
}
