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

static void checkNear(const char *what, float got, float want, float eps) {
  bool ok = fabsf(got - want) <= eps;
  if (!ok) printf("  got %.4f want %.4f\n", got, want);
  check(what, ok);
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

  printf("-- asleep ladder (the local sleep clock drives its bands)\n");
  BuddyPose p = buddyPose(BUDDY_ASLEEP, 1000, 1000, seed);
  check("under 60 s it is neutral and open", !p.closed && p.lid < 0.02f);
  p = buddyPose(BUDDY_ASLEEP, 70000, 70000, seed);
  check("60-120 s drifts the gaze", p.gazeX != 0.0f || p.lid > 0.05f);
  p = buddyPose(BUDDY_ASLEEP, 130000, 130000, seed);
  check("120-180 s drops the lids", p.lid >= 0.3f && !p.closed);
  p = buddyPose(BUDDY_ASLEEP, 190000, 190000, seed);
  check("180 s+ is asleep", p.closed);

  printf("-- new pose channels default to off\n");
  {
    BuddyPose base;
    check("hot starts at the owner colour", base.hot == 0.0f);
    check("per-eye rotation starts neutral",
          base.rotL == 0.0f && base.rotR == 0.0f);
    check("bob starts still", base.bob == 0.0f);
    check("the eye box is square", base.rx == base.ry);
  }

  printf("-- waiting (asking the owner): expectant -> pleading -> fidgeting\n");
  {
    // 0-4 s: attention held on the owner, eyes up and open, no squint.
    BuddyPose a = buddyPose(BUDDY_WAITING, 0, 0, seed);
    check("the expectant look points at the owner", a.gazeY < -3.0f);
    check("it stays open eyed", a.lid < 0.05f);
    check("it asks with a wide-ish eye", a.ry >= 50);
    check("the head is steady in the first band", fabsf(a.turn) < 0.01f);
    check("the sway is small, not a look-around", fabsf(a.gazeX) <= 5.5f);

    // 4-12 s: pleads, tilts and leans in towards the viewer.
    BuddyPose b = buddyPose(BUDDY_WAITING, 8000, 8000, seed);
    check("the head turns while asking", fabsf(b.turn) > 0.05f);
    check("it tilts the whole pair", fabsf(b.tilt) > 0.5f);
    check("the asking eye lowers its lid a hair", b.bottom > 0.05f);

    // 12 s+: fidgets, the look wanders further than the first band.
    BuddyPose c = buddyPose(BUDDY_WAITING, 20000, 20000, seed);
    check("it fidgets when nobody answers", fabsf(c.gazeX) > 6.0f);
    check("fidgeting turns the head further", fabsf(c.turn) > fabsf(b.turn));
    check("waiting never narrows the eyes in anger", c.hot < 0.01f);

    // Nothing in the ladder may read as a squint.
    bool anySquint = false;
    for (uint32_t t = 0; t < 20000; t += 137) {
      if (buddyPose(BUDDY_WAITING, t, t, seed).ry < 45) anySquint = true;
    }
    check("the whole ladder keeps the eyes open", !anySquint);
  }

  printf("-- error: startle -> frustration -> apology\n");
  {
    BuddyPose s = buddyPose(BUDDY_ERROR, 200, 10, seed);
    check("it startles crossed", s.eyeGazeL > 0 && s.eyeGazeR < 0);
    check("the startle blinks hard", s.blink);
    check("error tints hot", s.hot > 0.2f);

    BuddyPose f = buddyPose(BUDDY_ERROR, 3000, 3000, seed);
    check("frustration squeezes the eyes", f.ry < 45);
    check("frustration looks down", f.gazeY > 3.0f);
    check("frustration shakes", f.tremble > 0.3f);
    check("frustration is hotter than the startle", f.hot > s.hot);

    BuddyPose ap = buddyPose(BUDDY_ERROR, 9000, 9000, seed);
    check("the apology droops the lids", ap.lid > 0.35f);
    check("the apology turns away", ap.turn < 0.0f);
    check("the apology stops shaking", ap.tremble < 0.01f);
    check("the apology looks down", ap.gazeY > 6.0f);
  }

  printf("-- done bounces then relaxes\n");
  {
    BuddyPose happy = buddyPose(BUDDY_DONE, 500, 500, seed);
    BuddyPose late = buddyPose(BUDDY_DONE, 4000, 4000, seed);
    check("the happy second bounces", fabsf(happy.bob) > 0.5f);
    check("it relaxes after the happy second", !late.happy && late.lid > 0.05f);
    check("the afterglow raises the lower lid", late.bottom > 0.05f);
    check("the afterglow is not the full arc", !late.happy);
  }

  printf("-- the new channels default to the old look\n");
  {
    BuddyPose base;
    check("no tilt by default", base.tilt == 0.0f);
    check("no head turn by default", base.turn == 0.0f);
    check("no tilt by default", base.tilt == 0.0f);
    check("idle never turns the head", buddyPose(BUDDY_IDLE, 1000, 1000, seed).turn == 0.0f);
    check("tool never turns the head", buddyPose(BUDDY_TOOL, 5000, 5000, seed).turn == 0.0f);
  }

  printf("-- breathing while idle and asleep\n");
  {
    float bobA = buddyPose(BUDDY_IDLE, 0, 0, seed).bob;
    float bobB = buddyPose(BUDDY_IDLE, 0, 900, seed).bob;
    check("idle breathes", fabsf(bobA - bobB) > 0.2f);
    float asleep = buddyPose(BUDDY_ASLEEP, 190000, 190000, seed).bob;
    float asleepLater = buddyPose(BUDDY_ASLEEP, 190000, 191500, seed).bob;
    check("the face breathes until it falls asleep",
          fabsf(buddyPose(BUDDY_ASLEEP, 70000, 70000, seed).bob -
                buddyPose(BUDDY_ASLEEP, 70000, 71500, seed).bob) > 0.3f);
    check("and then it stops completely", asleep == 0.0f && asleepLater == 0.0f);
  }

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
  check("tool entry surprises (tall eyes, not touching)",
        p.surprise && p.ry > 56 && p.rx <= 50);
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
  check("tool entry surprises (tall eyes, not touching)",
        p.surprise && p.ry > 56 && p.rx <= 50);
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

  printf("-- microsaccades\n");
  {
    // S1: 1-3 px jumps, one every 2-5 s while idle.
    float maxMag = 0.0f;
    uint32_t lastStart = 0;
    uint32_t minGap = 0xFFFFFFFFu;
    uint32_t maxGap = 0;
    int jumps = 0;
    bool moved = false;
    float prevToX = 0.0f;
    float prevToY = 0.0f;
    bool out_of_range = false;
    for (uint32_t t = 0; t < 120000; t += 50) {
      BuddySaccade s = buddySaccadeAt(t, seed);
      if (fabsf(s.x) > 3.001f || fabsf(s.y) > 3.001f) out_of_range = true;
      float m = fabsf(s.toX) > fabsf(s.toY) ? fabsf(s.toX) : fabsf(s.toY);
      if (m > maxMag) maxMag = m;
      if (s.startMs != lastStart) {
        if (lastStart) {
          uint32_t gap = s.startMs - lastStart;
          if (gap < minGap) minGap = gap;
          if (gap > maxGap) maxGap = gap;
        }
        lastStart = s.startMs;
        jumps++;
        if (fabsf(s.toX - prevToX) > 0.5f || fabsf(s.toY - prevToY) > 0.5f) {
          moved = true;
        }
        prevToX = s.toX;
        prevToY = s.toY;
      }
    }
    check("a microsaccade is 1-3 px, never more", maxMag >= 1.0f && maxMag <= 3.001f);
    check("the drawn gaze stays inside 3 px", !out_of_range);
    check("the eye looks somewhere every 2-5 s", minGap >= 2000 && maxGap <= 5000);
    check("there is a new look every few seconds", jumps >= 20);
    check("the fixation lands on a new point", moved);
    uint32_t fire = 0;
    for (uint32_t t = 200; t < 20000; t += 200) {
      if (buddySaccadeAt(t, seed).startMs == t - 1) {
        fire = t - 1;
        break;
      }
    }
    BuddySaccade at0 = buddySaccadeAt(fire, seed);
    BuddySaccade atEnd = buddySaccadeAt(fire + BUDDY_SACCADE_JUMP_MS, seed);
    check("the saccade starts from the previous fixation",
          fabsf(at0.x - at0.fromX) < 0.01f && fabsf(at0.y - at0.fromY) < 0.01f);
    check("and lands inside one jump window", fabsf(atEnd.x - atEnd.toX) < 0.01f &&
                                                 fabsf(atEnd.y - atEnd.toY) < 0.01f);
    check("a saccade is quick, under 100 ms", BUDDY_SACCADE_JUMP_MS <= 100);
  }

  printf("-- side glances\n");
  {
    // S7: every so often the idle eye looks to a side and comes back.
    int glances = 0;
    uint32_t lastStart = 0xFFFFFFFFu;
    float maxAbs = 0.0f;
    bool back_to_centre = false;
    for (uint32_t t = 0; t < 120000; t += 50) {
      BuddyGlance g = buddyGlanceAt(t, seed);
      if (g.startMs != lastStart) {
        lastStart = g.startMs;
        glances++;
      }
      if (fabsf(g.x) > maxAbs) maxAbs = fabsf(g.x);
      if (!g.active) back_to_centre = true;
    }
    check("the idle face looks to a side now and then", glances >= 6);
    check("the glance is a wide look, not a saccade", maxAbs >= 8.0f);
    check("and it stays on the glass", maxAbs <= 20.0f);
    check("the face comes back to centre", back_to_centre);
    BuddyGlance down = buddyGlanceAt(0, seed);
    BuddyGlance after = buddyGlanceAt(BUDDY_GLANCE_MS + 4000, seed);
    check("the glance expires", !down.active || !after.active);
  }

  printf("-- blink bursts\n");
  {
    // S2: random 2-6 s, with bursts and the occasional long blink.
    bool blink = false;
    bool prev = false;
    int blinks = 0;
    int longBlink = 0;
    uint32_t opened = 0;
    uint32_t minGap = 0xFFFFFFFFu;
    uint32_t maxGap = 0;
    uint32_t last = 0;
    for (uint32_t t = 0; t < 120000; t += 10) {
      blink = buddyBlinkAt(t, seed);
      if (blink && !prev) {
        blinks++;
        if (last) {
          uint32_t gap = t - last;
          if (gap < minGap) minGap = gap;
          if (gap > maxGap) maxGap = gap;
        }
        last = t;
        opened = t;
      }
      if (!blink && prev && (t - opened) > 200) longBlink++;
      prev = blink;
    }
    check("the idle face blinks", blinks >= 10);
    check("but leaves the eyes open most of the time", blinks < 45);
    check("some gaps are short: a burst", minGap < 1500u);
    check("some gaps are long, so it is not a timer", maxGap > 4500u);
    check("every gap is inside 300 ms - 12 s", minGap >= 250u && maxGap <= 12000u);
    check("a burst comes right after a blink", minGap < 600u);
    check("the regular rhythm stays inside 2-6 s",
          maxGap <= 6200u && minGap < 1500u);
    check("there is at least one long blink", longBlink > 0);
  }

  printf("-- the asleep face freezes\n");
  {
    // Deep sleep past three minutes: the eyes shut and nothing moves at all.
    bool moved = false;
    for (uint32_t t = 0; t < 6000; t += 50) {
      BuddyPose a = buddyPose(BUDDY_ASLEEP, 190000, 1000 + t, seed);
      BuddyPose b = buddyPose(BUDDY_ASLEEP, 190000, 1050 + t, seed);
      if (!a.closed) moved = true;
      if (fabsf(a.bob) > 0.001f || fabsf(a.gazeX) > 0.001f ||
          fabsf(a.gazeY) > 0.001f) {
        moved = true;
      }
      if (a.blink || b.blink) moved = true;
    }
    BuddyPose asleep = buddyPose(BUDDY_ASLEEP, 190000, 5000, seed);
    check("deep sleep shuts the eyes", asleep.closed);
    check("and nothing at all happens there", !moved);
  }

  printf("-- idle distractions\n");
  {
    // S13: a bug crawls across the glass now and then; the eyes follow it.
    int events = 0;
    uint32_t lastId = 0xFFFFFFFFu;
    bool offscreen = false;
    bool wentWide = false;
    bool monotone = true;
    float prevX = 0;
    float lastStep = 0.0f;
    // A full day, because the bugs went from a 50 s drumbeat to the same rare
    // clock as every other idle event: roughly one an hour, never in your face.
    for (uint32_t t = 0; t < 24u * 3600u * 1000u; t += 200) {
      BuddyBug b = buddyBugAt(t, seed);
      if (b.active) {
        if (b.eventId != lastId) {
          events++;
          lastId = b.eventId;
          prevX = b.x;
          lastStep = 0.0f;
        }
        if (b.x < -25.0f || b.x > 345.0f || b.y < 10.0f || b.y > 160.0f) {
          offscreen = true;
        }
        float gx = 0, gy = 0;
        buddyBugGaze(b, &gx, &gy);
        if (fabsf(gx) > 22.01f || fabsf(gy) > 14.01f) offscreen = true;
        if (fabsf(gx) > 12.0f) wentWide = true;
        if (b.eventId == lastId) {
          // The bug never turns around inside one event: the step keeps its
          // sign for the whole crawl.
          float step = b.x - prevX;
          if (fabsf(step) > 0.5f) {
            if (lastStep != 0.0f && step * lastStep < 0.0f) monotone = false;
            lastStep = step;
          }
          prevX = b.x;
        }
      }
    }
    check("a bug shows up now and then", events >= 8);
    check("but the glass is not crawling with them", events <= 120);
    check("the bug stays on the glass", !offscreen);
    check("the eyes have to travel to follow it", wentWide);
    check("the bug crawls one way, it does not bounce", monotone);
    BuddyBug none = buddyBugAt(0, seed);
    check("the schedule is deterministic", none.active == buddyBugAt(0, seed).active);
  }

  printf("-- sleeping ZzZ\n");
  {
    // S15: three letters rise and fade on a loop, always moving up and right.
    int prevY[3] = {0, 0, 0};
    bool rises = true;
    bool fades = true;
    int minAlpha = 100, maxAlpha = 0;
    bool inBox = true;
    for (uint32_t t = 0; t < 4800; t += 40) {
      for (int i = 0; i < 3; i++) {
        int x = 0, y = 0, a = 0;
        buddyZzz(t, i, &x, &y, &a);
        if (x < 230 || x > 285 || y < 40 || y > 120) inBox = false;
        if (a < minAlpha) minAlpha = a;
        if (a > maxAlpha) maxAlpha = a;
        // A wrap from the top back to the bottom is the loop restarting, not a
        // fall: only a small backward step counts as going down.
        if (t > 0 && y > prevY[i] && (y - prevY[i]) < 40) rises = false;
        prevY[i] = y;
        if (a < 0 || a > 80) fades = false;
      }
    }
    check("a Z drifts up and to the right", rises);
    check("the letters stay next to the face", inBox);
    check("and fade as they rise", fades && minAlpha < 10 && maxAlpha > 60);
  }

  printf("-- waking up is an eased opening, not a pop\n");
  {
    // Item 7: from asleep the eyes open over ~100 ms. A shut eye is a different
    // shape (a bar), so the pose hands over to a nearly shut lid and the filter
    // does the rest; a hard pop is what this pins against.
    BuddyPose asleep;
    asleep.closed = true;
    asleep.lid = 0.0f;
    buddyWakeEyes(&asleep);
    check("the shut face lets go of the bar", !asleep.closed);
    check("and its target is a fully open eye", asleep.lid == 0.0f);
    check("waking is a startle", asleep.surprise && asleep.ry >= 60);
    check("and it is not a blink or a happy face",
          !asleep.blink && !asleep.happy);

    BuddyPose nodding;
    nodding.lid = 0.35f;
    nodding.lidR = 0.2f;
    nodding.blink = true;
    buddyWakeEyes(&nodding);
    check("a nodding face opens all the way and drops its blink",
          nodding.lid == 0.0f && nodding.lidR == 0.0f && nodding.surprise &&
              !nodding.blink);
  }

  printf("-- idle breathing\n");
  {
    // S3: a 4-5 s cycle moving the eye line about one pixel.
    checkNear("the breath has a 4.5 s cycle", buddyBreath(1000),
              buddyBreath(1000 + 4500), 0.01f);
    check("it moves", fabsf(buddyBreath(0) - buddyBreath(1125)) > 0.5f);
    float peak = 0.0f;
    float idlePeak = 0.0f;
    for (uint32_t t = 0; t < 4500; t += 20) {
      float b = buddyBreath(t);
      if (fabsf(b) > peak) peak = fabsf(b);
      float bob = buddyPose(BUDDY_IDLE, 1000, t, seed).bob;
      if (fabsf(bob) > idlePeak) idlePeak = fabsf(bob);
    }
    check("the breath is a full sine", peak > 0.98f);
    check("the idle eye line rises and falls about one pixel",
          idlePeak > 0.9f && idlePeak < 1.2f);
  }

  printf("-- idle events: rare, varied, and never on top of each other\n");
  {
    uint32_t seed2 = 0x5EED1234u;
    // Purity: the schedule is a function of the clock and the seed alone.
    BuddyIdleEvent a1 = buddyIdleEventAt(1234567u, seed2);
    BuddyIdleEvent a2 = buddyIdleEventAt(1234567u, seed2);
    check("the idle schedule is deterministic", a1.active == a2.active &&
                                                   a1.kind == a2.kind &&
                                                   a1.startMs == a2.startMs);

    // Rarity: over a simulated day, most of the clock has no event at all.
    const uint32_t DAY = 24u * 60u * 60u * 1000u;
    uint32_t busyMs = 0;
    uint32_t prevEnd = 0;
    bool overlap = false;
    uint32_t seen[16] = {0};
    int kinds = 0;
    BuddyIdleEvent last;
    for (uint32_t now = 0; now < DAY; now += 500u) {
      BuddyIdleEvent e = buddyIdleEventAt(now, seed2);
      if (!e.active) continue;
      busyMs += 500u;
      if (e.eventId != last.eventId) {
        // A new event: it must not start before the previous one ended.
        if (prevEnd != 0 && e.startMs < prevEnd) overlap = true;
        prevEnd = e.startMs + e.durationMs;
        last = e;
        if (e.kind < 16) seen[e.kind]++;
      }
    }
    check("idle events take under 8% of the day", busyMs * 100u < DAY * 8u);
    check("idle events take at least 1% of the day", busyMs * 100u > DAY * 1u);
    check("two idle events never overlap", !overlap);

    // Variety: every kind gets its turn over a day, so no event is dead code.
    for (int k = 1; k < (int)BUDDY_IDLE_COUNT; k++) {
      if (seen[k] > 0) kinds++;
    }
    check("every kind of idle event shows up", kinds == (int)BUDDY_IDLE_COUNT - 1);

    // Each event is short enough to live inside one epoch, so the "one event
    // per epoch" guarantee actually holds.
    bool tooLong = false;
    for (int k = 1; k < (int)BUDDY_IDLE_COUNT; k++) {
      if (buddyIdleDuration((uint8_t)k, 0x1234u) == 0) tooLong = true;
      if (buddyIdleDuration((uint8_t)k, 0x1234u) >= BUDDY_IDLE_EPOCH_MS) tooLong = true;
    }
    check("every event fits inside its epoch", !tooLong);

    // Every non-bug event has to actually move the eyes. This is the test that
    // keeps a new kind from being declared and then forgotten.
    bool allMove = true;
    for (int k = 1; k < (int)BUDDY_IDLE_COUNT; k++) {
      if (k == BUDDY_IDLE_BUG) continue;
      BuddyPose base;
      BuddyPose moved;
      BuddyIdleEvent e;
      e.active = true;
      e.kind = (uint8_t)k;
      e.hash = 0x00A50000u;
      e.durationMs = 3000;
      // Mid-event is where the effect peaks.
      e.t = 0.5f;
      buddyIdleApply(e, 1000u, &moved);
      bool same = moved.turn == base.turn && moved.tilt == base.tilt &&
                  moved.lid == base.lid && moved.lidL == base.lidL &&
                  moved.lidR == base.lidR && moved.gazeX == base.gazeX &&
                  moved.gazeY == base.gazeY && moved.bottom == base.bottom &&
                  moved.tremble == base.tremble && moved.ry == base.ry &&
                  !moved.surprise;
      if (same) allMove = false;
    }
    check("every idle event actually moves the eyes", allMove);

    // And none of them may run while Sofía is talking: the renderer cancels by
    // eventId, so the id has to be unique per event.
    uint32_t probe = 0;
    uint32_t probeEpoch = 0;
    for (uint32_t now = 0; now < DAY; now += 250u) {
      BuddyIdleEvent e = buddyIdleEventAt(now, seed2);
      if (e.active) {
        probe = e.eventId;
        probeEpoch = e.startMs / BUDDY_IDLE_EPOCH_MS;
        break;
      }
    }
    check("idle events carry the epoch as their id", probe == probeEpoch);

    // A shiver is short and a bug is long: the durations are not all the same.
    check("a shiver is quicker than a bug", buddyIdleDuration(BUDDY_IDLE_SHIVER, 1u) <
                                                 buddyIdleDuration(BUDDY_IDLE_BUG, 1u));
  }

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
