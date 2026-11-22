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

  printf("-- the pose the rhythm paints: the hop\n");
  // One beat at 120 bpm is 500 ms. Phase 0 is the floor (the hop begins), 0.31 is
  // the top of the arc, 0.62 is the landing squash.
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
  p.dim = true;
  buddyDancePose(buddyDanceBeatAt(120, 0), 0, &p);
  check("the dance takes the face back to its own channels",
        !p.happy && !p.closed && !p.surprise && !p.dim && p.hot == 0.0f &&
            p.lidL == 0.0f && p.tremble == 0.0f && p.glow == 1.0f &&
            p.eyeGazeL == 0.0f && p.grow == 1.0f);
  check("on the floor the eye is the neutral squircle",
        p.rx == 48 && p.ry == 48 && p.bob == 0.0f && p.lid == 0.0f);

  BuddyPose top;
  buddyDancePose(buddyDanceBeatAt(120, 155), 155, &top);
  check("in the air the eye leaves the floor", top.bob <= -9.5f && top.bob >= -10.5f);
  check("and it stretches taller", top.ry >= 53 && top.ry <= 55);
  check("and it widens a little", top.rx >= 50 && top.rx <= 51);
  check("the lids are open at the top", top.lid == 0.0f);
  check("the joy is the lower eyelid", top.bottom > 0.18f && top.bottom <= 0.21f);
  check("the gaze lifts with the jump", top.gazeY < -1.5f);

  BuddyPose land;
  buddyDancePose(buddyDanceBeatAt(120, 315), 315, &land);
  check("landing squashes the eye", land.ry <= 45 && land.ry >= 43);
  check("and drops the lid for an instant", land.lid > 0.05f);
  check("with the body back on the floor", fabsf(land.bob) < 1.0f);

  // The last beat of the bar is two small hops: at a quarter of it the pair is
  // already coming down from the first one.
  BuddyPose close;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 3 + 78), 1578, &close);
  check("the last beat hops twice, smaller", close.bob < -4.5f && close.bob > -5.5f);

  BuddyPose barA;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 2), 1000, &barA);
  BuddyPose barB;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 6), 3000, &barB);
  check("the pair rolls the other way on the next bar",
        barA.rot > 0.0f && barB.rot < 0.0f);
  check("and tips with the phrase", fabsf(barA.tilt) < 3.5f);

  BuddyPose blink;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 15 + 375), 7875, &blink);
  check("the dance blinks on its own count", blink.blink);
  BuddyPose noBlink;
  buddyDancePose(buddyDanceBeatAt(120, 500 * 8 + 375), 4375, &noBlink);
  check("and stays open off the count", !noBlink.blink);

  printf("-- the blend\n");
  BuddyPose base;
  base.ry = 30;
  base.bob = 4.0f;
  base.happy = true;
  BuddyPose over;
  over.ry = 50;
  over.bob = -8.0f;
  over.happy = false;
  BuddyPose mixed;
  buddyPoseMix(base, over, 0.0f, &mixed);
  check("a zero blend leaves the face alone", mixed.ry == 30 && mixed.bob == 4.0f);
  buddyPoseMix(base, over, 1.0f, &mixed);
  check("a full blend is the dance", mixed.ry == 50 && mixed.bob == -8.0f);
  buddyPoseMix(base, over, 0.5f, &mixed);
  check("halfway is halfway", mixed.ry == 40 && fabsf(mixed.bob + 2.0f) < 0.01f);
  check("a boolean stays with the face until the dance is most of the blend",
        mixed.happy);
  buddyPoseMix(base, over, 0.6f, &mixed);
  check("past the middle the dance owns the booleans too", !mixed.happy);

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
