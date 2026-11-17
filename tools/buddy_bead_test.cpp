// Host-side test for the rear bead (the owner's point 4, bead half).
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/buddy_bead_test.cpp -o /tmp/bbt && /tmp/bbt
//
// The WS2812 cannot be read back, so this is the only place the bead's behaviour
// can be proved: a finish twinkles green at 2 Hz for five seconds and fades back,
// an idle poll never re-lights it (the guard that keeps the twinkle a
// transition), and the face's mood outranks the phase light.

#include <cstdio>

#include "buddy_bead.h"

static int g_failures = 0;

static void check(const char *what, bool ok) {
  if (!ok) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void checkNear(const char *what, int got, int want, int eps) {
  bool ok = got >= want - eps && got <= want + eps;
  if (!ok) printf("  got %d want %d\n", got, want);
  check(what, ok);
}

// The twinkle is green (a touch of blue); the idle colour is cyan, so the blue
// channel tells them apart without guessing a brightness threshold.
static bool twinkle(BuddyRgb c) { return c.g > 140 && c.b < 100; }

static bool same(BuddyRgb a, BuddyRgb b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

static BuddyRgb tick(BuddyBead *b, BuddyPhase phase, uint32_t nowMs) {
  return buddyBeadTick(b, phase, buddyPhaseDefaultLed(phase), true,
                       BUDDY_MOOD_CALM, nowMs);
}

int main() {
  printf("-- phase colours\n");
  {
    BuddyBead b;
    BuddyRgb c = tick(&b, BUDDY_PHASE_IDLE, 1000);
    checkNear("idle is the phase colour at 80%", c.g, 144, 2);
    check("and keeps the hue", c.r == 0 && c.b > 190);
    c = tick(&b, BUDDY_PHASE_BUSY, 1200);
    check("busy is the bridge's colour, untouched",
          same(c, buddyPhaseDefaultLed(BUDDY_PHASE_BUSY)));
    tick(&b, BUDDY_PHASE_ASK, 1400);
    check("ask is a slow pulse, not a steady light",
          !same(tick(&b, BUDDY_PHASE_ASK, 2000),
                tick(&b, BUDDY_PHASE_ASK, 2800)));
    BuddyBead ok;
    tick(&ok, BUDDY_PHASE_BUSY, 100);
    c = tick(&ok, BUDDY_PHASE_OK, 200);
    check("ok is the dim cyan, not a green flash",
          c.r == 0 && c.g > 130 && c.g < 160);
  }

  printf("-- a finish twinkles and then fades\n");
  {
    BuddyBead b;
    tick(&b, BUDDY_PHASE_BUSY, 0);
    BuddyRgb on = tick(&b, BUDDY_PHASE_IDLE, 100);   // Desktop/Telegram finish
    check("the drop from busy to idle lights the bead", on.g > 140 && on.r == 0);
    BuddyRgb off = tick(&b, BUDDY_PHASE_IDLE, 400);
    check("and it blinks off 250 ms later", same(off, {0, 0, 0}));
    BuddyRgb again = tick(&b, BUDDY_PHASE_IDLE, 600);
    check("then back on: a twinkle, not a steady colour", again.g > 140);
    BuddyRgb fading = tick(&b, BUDDY_PHASE_IDLE, 5150);
    check("after five seconds it is on its way back to cyan",
          fading.b > 60 && fading.g > 100);
    BuddyRgb settled = tick(&b, BUDDY_PHASE_IDLE, 5600);
    check("and it settles on the idle colour",
          settled.r == 0 && settled.b > 190 && settled.g < 160);
  }

  printf("-- the guard: idle polls never re-light it\n");
  {
    BuddyBead b;
    tick(&b, BUDDY_PHASE_BUSY, 0);
    tick(&b, BUDDY_PHASE_IDLE, 100);
    // Tick continuously, the way the loop does: the twinkle has to finish on its
    // own and then never come back while the bridge keeps saying idle.
    int lit = 0;
    for (uint32_t t = 4000; t < 60000; t += 100) {
      BuddyRgb c = tick(&b, BUDDY_PHASE_IDLE, t);
      if (t > 5400 && twinkle(c)) lit++;
    }
    check("after the twinkle, an hour of idle polls never re-lights it", lit == 0);
    check("the bead stays on the idle colour the whole time",
          tick(&b, BUDDY_PHASE_IDLE, 61000).b > 190);
  }

  printf("-- a new finish twinkles again\n");
  {
    BuddyBead b;
    tick(&b, BUDDY_PHASE_BUSY, 0);
    tick(&b, BUDDY_PHASE_IDLE, 100);
    for (uint32_t t = 6000; t < 20000; t += 500) tick(&b, BUDDY_PHASE_IDLE, t);
    tick(&b, BUDDY_PHASE_BUSY, 21000);
    BuddyRgb c = tick(&b, BUDDY_PHASE_IDLE, 21100);
    check("busy -> idle is a new finish", c.g > 140 && c.r == 0);
  }

  printf("-- ok -> idle is not a finish\n");
  {
    // The bridge's own ok starts the twinkle; the idle that follows must not
    // start a second one, only let the first one finish.
    BuddyBead b;
    tick(&b, BUDDY_PHASE_BUSY, 0);
    tick(&b, BUDDY_PHASE_OK, 100);
    int lit = 0;
    int again = 0;
    for (uint32_t t = 200; t < 20000; t += 100) {
      BuddyRgb c = tick(&b, BUDDY_PHASE_IDLE, t);
      if (t < 5000 && twinkle(c)) lit++;
      if (t > 5500 && twinkle(c)) again++;
    }
    check("the ok twinkle keeps running while idle follows", lit >= 4);
    check("and idle never starts a second twinkle", again == 0);
  }

  printf("-- busy cancels a celebration\n");
  {
    BuddyBead b;
    tick(&b, BUDDY_PHASE_BUSY, 0);
    tick(&b, BUDDY_PHASE_IDLE, 100);
    BuddyRgb c = tick(&b, BUDDY_PHASE_BUSY, 300);
    check("work wins over the celebration",
          same(c, buddyPhaseDefaultLed(BUDDY_PHASE_BUSY)));
  }

  printf("-- the face's mood outranks the phase\n");
  {
    BuddyBead b;
    tick(&b, BUDDY_PHASE_IDLE, 0);
    BuddyRgb red = buddyBeadTick(&b, BUDDY_PHASE_IDLE, buddyPhaseDefaultLed(BUDDY_PHASE_IDLE),
                                 true, BUDDY_MOOD_ANGRY, 100);
    check("anger is red", red.r > 200 && red.g < 50 && red.b < 50);
    BuddyRgb dim = buddyBeadTick(&b, BUDDY_PHASE_IDLE, buddyPhaseDefaultLed(BUDDY_PHASE_IDLE),
                                 true, BUDDY_MOOD_ANGRY, 400);
    check("and it pulses instead of glowing flat",
          dim.r < red.r && dim.r > 40);
    BuddyRgb ember = buddyBeadTick(&b, BUDDY_PHASE_IDLE, buddyPhaseDefaultLed(BUDDY_PHASE_IDLE),
                                   true, BUDDY_MOOD_SLEEPING, 100);
    check("sleeping is a faint ember of the eye colour",
          ember.r == 0 && ember.g <= 14 && ember.b <= 20);
    BuddyRgb busyRage = buddyBeadTick(&b, BUDDY_PHASE_BUSY, buddyPhaseDefaultLed(BUDDY_PHASE_BUSY),
                                      true, BUDDY_MOOD_ANGRY, 100);
    check("anger outranks a busy phase too", busyRage.r > 200 && busyRage.g < 50);
  }

  printf("-- a missing led field falls back to the table\n");
  {
    BuddyBead b;
    BuddyRgb c = buddyBeadTick(&b, BUDDY_PHASE_BUSY, {0, 0, 0}, false,
                               BUDDY_MOOD_CALM, 100);
    check("busy uses the published colour",
          same(c, buddyPhaseDefaultLed(BUDDY_PHASE_BUSY)));
    c = buddyBeadTick(&b, BUDDY_PHASE_ASK, {9, 9, 9}, false, BUDDY_MOOD_CALM, 200);
    check("ask falls back too", c.r > c.g && c.g == c.b);
  }

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
