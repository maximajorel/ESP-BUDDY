// The rear bead's colour, pure and header-only.
//
// The owner's point 4 is about the bead, not only the glass: the twinkle is a
// transition (a finish), never a state, so an idle poll must never re-light it.
// A single WS2812 cannot be read back, so the only honest way to know the bead
// is right is to keep the decision here and pin it with `tools/buddy_bead_test.cpp`;
// `buddy_led.cpp` is left with nothing but "ask the view, then paint".
#pragma once

#include <math.h>
#include <stdint.h>

#include "buddy_imu.h"    // BuddyMood
#include "buddy_phase.h"  // BuddyPhase, BuddyRgb and its tables

// A finish blinks green at 2 Hz for five seconds, then fades back to the identity
// colour in 300 ms. Same numbers the bead shipped with, named so the test and the
// firmware cannot drift.
#define BUDDY_BEAD_CELEBRATE_MS 5000u
#define BUDDY_BEAD_FADE_MS 300u
#define BUDDY_BEAD_BLINK_HALF_MS 250u
#define BUDDY_BEAD_CELEBRATE_SCALE 0.6f
#define BUDDY_BEAD_IDLE_SCALE 0.8f
#define BUDDY_BEAD_SLEEP_SCALE 0.07f
#define BUDDY_BEAD_ANGRY_ON_MS 300u
#define BUDDY_BEAD_ANGRY_PERIOD_MS 500u

// The bead's memory: only a phase *change* may start a celebration, which is
// exactly the guard the owner asked for.
struct BuddyBead {
  bool havePrev = false;
  BuddyPhase prev = BUDDY_PHASE_IDLE;
  bool celebrating = false;
  uint32_t celebrationStart = 0;
  bool fading = false;
  uint32_t fadeStart = 0;
};

static inline BuddyRgb buddyBeadScale(BuddyRgb c, float k) {
  if (k < 0.0f) k = 0.0f;
  if (k > 1.0f) k = 1.0f;
  return {(uint8_t)((float)c.r * k), (uint8_t)((float)c.g * k),
          (uint8_t)((float)c.b * k)};
}

static inline BuddyRgb buddyBeadMix(BuddyRgb from, BuddyRgb to, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return {(uint8_t)((float)from.r + ((float)to.r - (float)from.r) * t),
          (uint8_t)((float)from.g + ((float)to.g - (float)from.g) * t),
          (uint8_t)((float)from.b + ((float)to.b - (float)from.b) * t)};
}

// What the bead shows now. Call once per loop with the view's phase and LED, the
// face's mood, and the clock.
static inline BuddyRgb buddyBeadTick(BuddyBead *b, BuddyPhase phase, BuddyRgb led,
                                     bool ledValid, BuddyMood mood,
                                     uint32_t nowMs) {
  if (!b->havePrev) {
    b->havePrev = true;
    b->prev = BUDDY_PHASE_IDLE;
  }
  if (phase != b->prev) {
    if (buddyPhaseFinished(b->prev, phase)) {
      // The bridge's ok, or the busy -> idle step Desktop and Telegram produce.
      b->celebrating = true;
      b->celebrationStart = nowMs;
      b->fading = false;
    } else if (phase == BUDDY_PHASE_BUSY || phase == BUDDY_PHASE_ASK) {
      // Work wins over the celebration.
      b->celebrating = false;
      b->fading = false;
    }
    b->prev = phase;
  }

  // The face's mood outranks the phase light: red while it is angry, a faint
  // ember while it sleeps on the table.
  if (mood == BUDDY_MOOD_ANGRY) {
    BuddyRgb red = {255, 30, 20};
    bool on = (nowMs % BUDDY_BEAD_ANGRY_PERIOD_MS) < BUDDY_BEAD_ANGRY_ON_MS;
    return buddyBeadScale(red, on ? 1.0f : 0.3f);
  }
  if (mood == BUDDY_MOOD_SLEEPING) {
    return buddyBeadScale(buddyEyesCyan(), BUDDY_BEAD_SLEEP_SCALE);
  }

  if (b->celebrating) {
    uint32_t elapsed = nowMs - b->celebrationStart;
    if (elapsed >= BUDDY_BEAD_CELEBRATE_MS) {
      b->celebrating = false;
      b->fading = true;
      b->fadeStart = nowMs;
    } else {
      BuddyRgb green = buddyPhaseDefaultLed(BUDDY_PHASE_OK);
      bool on = (elapsed % (2u * BUDDY_BEAD_BLINK_HALF_MS)) <
                BUDDY_BEAD_BLINK_HALF_MS;
      return buddyBeadScale(green, on ? BUDDY_BEAD_CELEBRATE_SCALE : 0.0f);
    }
  }

  if (b->fading) {
    uint32_t elapsed = nowMs - b->fadeStart;
    if (elapsed >= BUDDY_BEAD_FADE_MS) {
      b->fading = false;
    } else {
      float t = (float)elapsed / (float)BUDDY_BEAD_FADE_MS;
      return buddyBeadMix(buddyPhaseDefaultLed(BUDDY_PHASE_OK), buddyEyesCyan(),
                          t);
    }
  }

  BuddyRgb color = ledValid ? led : buddyPhaseDefaultLed(phase);
  switch (phase) {
    case BUDDY_PHASE_ASK: {
      // Slow 1.5 s pulse, 0.4..1.0: an approval that waits is not a steady light.
      float pulse =
          0.4f + 0.6f * (0.5f + 0.5f * sinf((float)nowMs * 0.00418879f - 1.5708f));
      return buddyBeadScale(color, pulse);
    }
    case BUDDY_PHASE_BUSY:
      return color;
    case BUDDY_PHASE_OK:
      // `ok` lasts ~8 s at the bridge but the blink is already over: stay dim.
      return buddyBeadScale(buddyEyesCyan(), 0.8f);
    case BUDDY_PHASE_IDLE:
    default:
      return buddyBeadScale(color, BUDDY_BEAD_IDLE_SCALE);
  }
}
