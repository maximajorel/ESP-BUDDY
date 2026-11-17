// Face poses for Sofía's buddy, pure and header-only.
//
// The bridge reports a state and `since`; this module turns that into what the
// eyes should look like right now. It is deliberately free of Arduino so
// `tools/buddy_anim_test.cpp` can pin the ladders (bored, long thinking, tool
// surprise) and the anti-flicker hold without a board.
//
// The design follows Sofía's brief: the server names the state, the firmware
// owns duration and emotion. The eyes are solid, so the language is gaze,
// eyelid, shape and blink rhythm.
#pragma once

#include <math.h>
#include <stdint.h>

#include "buddy_state.h"

// A state that lasts less than this is not drawn: the bridge already smooths
// tool/thinking, and this keeps a one-poll flicker from flashing the face.
#define BUDDY_MIN_STATE_MS 300

struct BuddyPose {
  float gazeX = 0;   // whole-eye offset in px
  float gazeY = 0;
  float eyeGazeL = 0;  // extra per-eye offset, used for the cross-eyed error
  float eyeGazeR = 0;
  int rx = 48;
  int ry = 48;
  float lid = 0;       // 0 open .. 1 covered from the top by a black band
  float lidL = 0;      // extra top lid for the left eye (asymmetric squint)
  float lidR = 0;      // extra top lid for the right eye
  float bottom = 0;    // 0 open .. 1 cut from the bottom (tired, EMO-style)
  float rot = 0;       // degrees for both eyes, positive = clockwise
  float rotL = 0;      // extra rotation for the left eye (anger, dizziness)
  float rotR = 0;      // extra rotation for the right eye
  float hot = 0;       // 0 = owner colour .. 1 = hot red (anger, dizziness)
  float bob = 0;       // vertical offset in px: breathing and bouncing
  bool blink = false;
  bool happy = false;
  bool closed = false;
  bool surprise = false;
  bool dim = false;
};

static inline uint32_t buddyHash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Deterministic 0..1 noise: the same seed and salt give the same "random"
// blink or saccade on every frame, so the face is stable between draws.
static inline float buddyUnit(uint32_t seed, uint32_t salt) {
  return (buddyHash32(seed * 0x9e3779b9u + salt) & 0xFFFFFFu) / 16777216.0f;
}

// Which state the glass should show. A brand new state younger than the hold
// window is deferred: if it disappears on the next poll it never flashed.
static inline BuddyState buddyShownState(BuddyState shown, BuddyState raw,
                                         uint32_t rawAgeMs) {
  if (raw == shown) return shown;
  if (rawAgeMs < BUDDY_MIN_STATE_MS) return shown;
  return raw;
}

// One gesture slot: the doc wants a new one every 3-6 s while the state does
// not change. The duration comes from the seed and the slot, so the rhythm is
// irregular instead of a mechanical 4 s beat.
struct BuddyGesture {
  uint32_t slot;
  uint32_t startMs;
};

static inline BuddyGesture buddyGestureAt(uint32_t ageMs, uint32_t seed) {
  BuddyGesture g = {0, 0};
  uint32_t start = 0;
  for (;;) {
    uint32_t duration =
        3000u + (buddyHash32(seed + g.slot * 0x9E3779B9u) % 3000u);
    if (start + duration > ageMs) {
      g.startMs = start;
      return g;
    }
    start += duration;
    g.slot++;
  }
}

static inline BuddyPose buddyPose(BuddyState state, uint32_t ageMs,
                                  uint32_t nowMs, uint32_t seed) {
  BuddyPose p;
  bool genericBlink = true;

  switch (state) {
    case BUDDY_UNCONFIGURED:
      p.dim = true;
      genericBlink = false;
      break;
    case BUDDY_OFFLINE:
      // No bridge: a dim, quiet face. Not an error face.
      p.dim = true;
      genericBlink = false;
      break;
    case BUDDY_DONE:
      // One second of satisfaction, then a relaxed open face for the rest of
      // the bridge's ~8 s done window. The little bounce is the "yes!".
      genericBlink = false;
      if (ageMs < 1200) {
        p.happy = true;
        p.bob = sinf((float)nowMs * 0.010f) * 2.5f;
      } else {
        p.lid = 0.15f;
        p.bob = sinf((float)nowMs * 0.0016f) * 1.0f;
      }
      break;
    case BUDDY_ERROR:
      // Confusion: eyes point at each other and blink fast.
      genericBlink = false;
      p.eyeGazeL = 7;
      p.eyeGazeR = -7;
      p.lid = 0.05f;
      p.blink = (nowMs % 900) < 120;
      break;
    case BUDDY_WAITING:
      // Waiting for an approval is nervous, not static: the look swings between
      // the owner and the sides, with a small impatient bob.
      p.rx = 50;
      p.ry = 50;
      p.gazeX = sinf((float)ageMs * 0.0012f) * 14.0f;
      p.gazeY = -2.0f;
      p.bob = sinf((float)nowMs * 0.004f) * 3.0f;
      break;
    case BUDDY_TOOL: {
      // A tool starting is a small surprise: wide for 200 ms, then the cycle.
      if (ageMs < 200) {
        p.surprise = true;
        p.rx = 50;
        p.ry = 60;
        genericBlink = false;
        break;
      }
      BuddyGesture g = buddyGestureAt(ageMs, seed ^ 0x51ed270bu);
      uint32_t count = ageMs > 30000u ? 5u : 4u;
      uint32_t which = g.slot % count;
      uint32_t inSlot = ageMs - g.startMs;
      switch (which) {
        case 0:  // reading: narrowed with a lateral micro-movement
          p.ry = 34;
          p.lid = 0.15f;
          p.gazeX = sinf((float)inSlot * 0.004f) * 4.0f;
          break;
        case 1:  // focus: one eye a little more closed than the other
          p.ry = 36;
          p.lidL = 0.3f;
          p.lidR = 0.1f;
          p.rot = 3.0f;
          break;
        case 2:  // tension up: wider and up
          p.ry = 52;
          p.gazeY = -5.0f;
          break;
        case 3:  // a rapid double blink at the start of the slot
          p.lid = 0.05f;
          p.blink = (inSlot < 120) || (inSlot >= 240 && inSlot < 360);
          break;
        case 4:  // the long tool sigh
          p.lid = 0.2f;
          p.gazeY = 3.0f;
          break;
      }
      break;
    }
    case BUDDY_THINKING: {
      BuddyGesture g = buddyGestureAt(ageMs, seed);
      uint32_t count = ageMs > 20000u ? 6u : 5u;
      uint32_t which = g.slot % count;
      uint32_t inSlot = ageMs - g.startMs;
      switch (which) {
        case 0:  // remembering: up-left, tilted, the right eye squints
          p.gazeX = -12;
          p.gazeY = -12;
          p.rot = 8.0f;
          p.lidR = 0.3f;
          break;
        case 1:  // concentration: narrowed and asymmetric
          p.ry = 32;
          p.lid = 0.12f;
          p.lidL = 0.22f;
          p.rot = 4.0f;
          p.gazeX = -4;
          p.gazeY = -10;
          break;
        case 2:  // looks at the owner with a blink
          p.ry = 38;
          p.blink = inSlot < 220;
          break;
        case 3:  // searching: up-right
          p.gazeX = 12;
          p.gazeY = -12;
          p.rot = -6.0f;
          p.lidL = 0.25f;
          break;
        case 4:  // long blink
          p.ry = 36;
          p.lid = 0.15f;
          p.blink = inSlot < 300;
          break;
        case 5:  // "uf, it is taking long": looks at the owner, bottom lid cut
          p.ry = 36;
          p.bottom = 0.35f;
          p.lid = 0.1f;
          p.gazeY = 2.0f;
          break;
      }
      break;
    }
    case BUDDY_BORED:
      if (ageMs < 20000) {
        // Neutral, normal blink.
      } else if (ageMs < 60000) {
        // Gaze drifts to the sides and comes back, with a micro sigh.
        p.gazeX = sinf((float)ageMs * 0.0009f) * 14.0f;
        p.lid = 0.08f;
      } else if (ageMs < 120000) {
        // Starts to nod: the lids drop and bounce.
        p.lid = 0.35f;
        p.gazeY = sinf((float)ageMs * 0.002f) * 3.0f;
      } else {
        // Asleep with a slow breath; only a real state change wakes it.
        p.closed = true;
        genericBlink = false;
      }
      break;
    case BUDDY_IDLE:
    default:
      break;
  }

  if ((state == BUDDY_THINKING || state == BUDDY_TOOL) && p.bob == 0.0f) {
    // A slow breath under the gesture cycle, so a static pose still moves.
    p.bob = sinf((float)nowMs * 0.0018f) * 0.8f;
  }

  if (state == BUDDY_IDLE) {
    // Microsaccades: a small jump of the whole eye every ~3 s plus a slow
    // sway and a breath. This is the detail that makes solid eyes feel alive.
    uint32_t block = nowMs / 3000;
    p.gazeX += buddyUnit(seed, block) * 12.0f - 6.0f;
    p.gazeY += buddyUnit(seed, block + 99) * 8.0f - 4.0f;
    p.gazeX += sinf((float)nowMs * 0.0011f) * 3.0f;
    p.bob = sinf((float)nowMs * 0.0016f) * 1.4f;
  }

  if (state == BUDDY_BORED) {
    // The breath never stops, not even asleep: it is the only motion left.
    p.bob = p.closed ? sinf((float)nowMs * 0.0015f) * 2.0f
                     : sinf((float)nowMs * 0.0013f) * 1.2f;
  }

  if (genericBlink && !p.blink && !p.happy && !p.closed && !p.dim) {
    // Blink every 2.6-6 s, with an occasional double blink.
    uint32_t period = 2600 + (buddyHash32(seed) % 3400);
    uint32_t phase = buddyHash32(seed ^ 0x9e3779b9u) % period;
    uint32_t t = (nowMs + phase) % period;
    uint32_t slot = (nowMs + phase) / period;
    bool burst = (buddyHash32(seed ^ slot) % 4u) == 0;
    p.blink = (t < 130) || (burst && t > 260 && t < 380);
  }
  return p;
}
