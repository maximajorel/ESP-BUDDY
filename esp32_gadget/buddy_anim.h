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
  // Expression channels added by the life pass. All default to the previous
  // look, so every existing expression is unchanged at zero. There are no
  // brows on purpose: the only upper feature of this face is the black eyelid.
  float grow = 1.0f;   // eye scale: anger makes the eye bigger
  float tremble = 0;   // 0..1 shake the renderer adds after the easing
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

// --- idle life ---------------------------------------------------------------
//
// The idle face is not a still picture: it holds a fixation, jumps 1-3 px to a
// new one, breathes, blinks on a rhythm that is not a timer, and every so often
// looks to a side. Everything is a deterministic hash of the clock, so the face
// is reproducible frame to frame and a test can pin the rhythm without a board.

// A saccade lands on a 3.6 s slot whose offset is hashed, so consecutive jumps
// are 2.0-5.0 s apart instead of a metronome beat.
#define BUDDY_SACCADE_TICK_MS 200
#define BUDDY_SACCADE_EPOCH_TICKS 18  // 3.6 s
#define BUDDY_SACCADE_SPREAD_TICKS 8  // the jump lands in the first 1.4 s
#define BUDDY_SACCADE_JUMP_MS 60      // how long the eye takes to arrive
#define BUDDY_SACCADE_MAX_PX 3.0f

struct BuddySaccade {
  float fromX = 0, fromY = 0;  // the previous fixation
  float toX = 0, toY = 0;      // where this jump landed
  float x = 0, y = 0;          // what to draw right now
  uint32_t startMs = 0;        // when this jump began
};

static inline uint32_t buddySaccadeTick(uint32_t epoch, uint32_t seed) {
  return epoch * BUDDY_SACCADE_EPOCH_TICKS +
         (buddyHash32(seed ^ (epoch * 0x9E3779B9u)) %
          BUDDY_SACCADE_SPREAD_TICKS);
}

// Landing point: 1-3 px horizontally, up to 2 px vertically, both signs.
static inline void buddySaccadeTarget(uint32_t epoch, uint32_t seed, float *x,
                                      float *y) {
  uint32_t h = buddyHash32(seed ^ (epoch * 0x85EBCA6Bu));
  float mx = 1.0f + (float)(h % 3u);
  float my = (float)((h >> 4) % 3u);
  *x = ((h >> 8) & 1u) ? mx : -mx;
  *y = ((h >> 9) & 1u) ? my : -my;
}

// The fixation in progress. The first 60 ms are the jump itself, eased so a
// 1 px saccade does not alias into a pop; after that the eye just holds.
static inline BuddySaccade buddySaccadeAt(uint32_t nowMs, uint32_t seed) {
  BuddySaccade s;
  uint32_t tick = nowMs / BUDDY_SACCADE_TICK_MS;
  uint32_t epoch = tick / BUDDY_SACCADE_EPOCH_TICKS;
  bool haveCur = false, havePrev = false;
  uint32_t curEpoch = 0, prevEpoch = 0;
  for (uint32_t i = 0; i < 6; i++) {
    if (epoch < i) break;
    uint32_t e = epoch - i;
    uint32_t fired = buddySaccadeTick(e, seed);
    if (fired > tick) continue;
    if (!haveCur) {
      haveCur = true;
      curEpoch = e;
    } else {
      havePrev = true;
      prevEpoch = e;
      break;
    }
  }
  if (!haveCur) return s;
  buddySaccadeTarget(curEpoch, seed, &s.toX, &s.toY);
  if (havePrev) buddySaccadeTarget(prevEpoch, seed, &s.fromX, &s.fromY);
  s.startMs = buddySaccadeTick(curEpoch, seed) * BUDDY_SACCADE_TICK_MS;
  float t = (float)(nowMs - s.startMs) / (float)BUDDY_SACCADE_JUMP_MS;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  float e = t * t * (3.0f - 2.0f * t);
  s.x = s.fromX + (s.toX - s.fromX) * e;
  s.y = s.fromY + (s.toY - s.fromY) * e;
  return s;
}

// A side glance: one per 12 s slot, 1.4-1.8 s long, 10-18 px out and back. The
// reach is what lets the renderer turn the pair instead of sliding it.
#define BUDDY_GLANCE_TICK_MS 250
#define BUDDY_GLANCE_EPOCH_TICKS 48  // one look per 12 s
#define BUDDY_GLANCE_SPREAD_TICKS 40
#define BUDDY_GLANCE_MS 1400
#define BUDDY_GLANCE_RISE_MS 140

struct BuddyGlance {
  float x = 0;
  bool active = false;
  uint32_t startMs = 0;
};

static inline uint32_t buddyGlanceTick(uint32_t epoch, uint32_t seed) {
  return epoch * BUDDY_GLANCE_EPOCH_TICKS +
         (buddyHash32(seed ^ (epoch * 0x27D4EB2Fu)) % BUDDY_GLANCE_SPREAD_TICKS);
}

static inline BuddyGlance buddyGlanceAt(uint32_t nowMs, uint32_t seed) {
  BuddyGlance g;
  uint32_t tick = nowMs / BUDDY_GLANCE_TICK_MS;
  uint32_t epoch = tick / BUDDY_GLANCE_EPOCH_TICKS;
  for (uint32_t i = 0; i < 3; i++) {
    if (epoch < i) break;
    uint32_t e = epoch - i;
    uint32_t fired = buddyGlanceTick(e, seed);
    if (fired > tick) continue;
    g.startMs = fired * BUDDY_GLANCE_TICK_MS;
    uint32_t h = buddyHash32(seed ^ (e * 0xC2B2AE35u));
    uint32_t hold = BUDDY_GLANCE_MS + (h % 400u);
    uint32_t in = nowMs - g.startMs;
    if (in >= hold) return g;
    float reach = 10.0f + (float)((h >> 4) % 9u);
    float dir = ((h >> 8) & 1u) ? 1.0f : -1.0f;
    float env = 1.0f;
    if (in < BUDDY_GLANCE_RISE_MS) {
      env = (float)in / (float)BUDDY_GLANCE_RISE_MS;
    } else if (hold - in < BUDDY_GLANCE_RISE_MS) {
      env = (float)(hold - in) / (float)BUDDY_GLANCE_RISE_MS;
    }
    if (env < 0.0f) env = 0.0f;
    g.x = reach * dir * env;
    g.active = true;
    return g;
  }
  return g;
}

// Blinking: a 4 s slot with a hashed offset, so consecutive blinks are 2.1-5.9 s
// apart; three blinks in ten also fire a companion 300 ms later (the burst),
// and one in five stays shut longer. A fixed timer is what gives an animation
// away; this is deliberately uneven.
#define BUDDY_BLINK_TICK_MS 100
#define BUDDY_BLINK_EPOCH_TICKS 40  // 4 s
#define BUDDY_BLINK_SPREAD_TICKS 20
#define BUDDY_BLINK_MS 130
#define BUDDY_BLINK_LONG_MS 260
#define BUDDY_BLINK_BURST_TICKS 3  // 300 ms after the first blink
#define BUDDY_BLINK_BURST_PERCENT 30u
#define BUDDY_BLINK_LONG_PERCENT 20u

static inline uint32_t buddyBlinkTick(uint32_t epoch, uint32_t seed) {
  return epoch * BUDDY_BLINK_EPOCH_TICKS +
         (buddyHash32(seed ^ (epoch * 0x9E3779B9u)) % BUDDY_BLINK_SPREAD_TICKS);
}

static inline bool buddyBlinkAt(uint32_t nowMs, uint32_t seed) {
  uint32_t tick = nowMs / BUDDY_BLINK_TICK_MS;
  uint32_t epoch = tick / BUDDY_BLINK_EPOCH_TICKS;
  uint32_t last = 0;
  bool have = false;
  for (uint32_t i = 0; i < 2; i++) {
    if (epoch < i) break;
    uint32_t e = epoch - i;
    uint32_t h = buddyHash32(seed ^ (e * 0x9E3779B9u));
    uint32_t base = buddyBlinkTick(e, seed);
    bool burst = ((h >> 8) % 100u) < BUDDY_BLINK_BURST_PERCENT;
    if (base <= tick && (!have || base > last)) {
      last = base;
      have = true;
    }
    uint32_t second = base + BUDDY_BLINK_BURST_TICKS;
    if (burst && second <= tick && (!have || second > last)) {
      last = second;
      have = true;
    }
  }
  if (!have) return false;
  uint32_t epochOfLast = last / BUDDY_BLINK_EPOCH_TICKS;
  uint32_t h = buddyHash32(seed ^ (epochOfLast * 0x51ED270Bu));
  uint32_t dur = ((h >> 4) % 100u) < BUDDY_BLINK_LONG_PERCENT
                     ? BUDDY_BLINK_LONG_MS
                     : BUDDY_BLINK_MS;
  return (nowMs - last * BUDDY_BLINK_TICK_MS) < dur;
}

// The breath: one slow sine, -1..1, 4.5 s per cycle. The eye line rides it, so
// the face keeps rising and falling even when nothing else happens.
#define BUDDY_BREATH_MS 4500.0f

static inline float buddyBreath(uint32_t nowMs) {
  return sinf((float)nowMs * (6.2831853f / BUDDY_BREATH_MS));
}

// --- idle distractions --------------------------------------------------------
//
// Every so often the idle face finds something to look at: a bug crawls across
// the glass and the eyes follow it. The event is scheduled in 50 s windows, so
// two events never overlap, and it dies the instant anything else happens on the
// bridge: the owner talking to Sofía always wins over the bug.
#define BUDDY_EVENT_EPOCH_MS 50000u
#define BUDDY_EVENT_SPREAD_MS 38000u  // the bug lands anywhere in the window
#define BUDDY_EVENT_MIN_MS 7000u
#define BUDDY_EVENT_MAX_MS 12000u

struct BuddyBug {
  bool active = false;
  uint32_t eventId = 0;  // the epoch, so the renderer can cancel this one
  float x = 0;           // screen px
  float y = 0;
  uint32_t startMs = 0;
  uint32_t durationMs = 0;
};

static inline BuddyBug buddyBugAt(uint32_t nowMs, uint32_t seed) {
  BuddyBug b;
  uint32_t epoch = nowMs / BUDDY_EVENT_EPOCH_MS;
  uint32_t base = epoch * BUDDY_EVENT_EPOCH_MS;
  uint32_t h = buddyHash32(seed ^ (epoch * 0xB5297A4Du));
  uint32_t start = base + (h % BUDDY_EVENT_SPREAD_MS);
  uint32_t duration =
      BUDDY_EVENT_MIN_MS + ((h >> 8) % (BUDDY_EVENT_MAX_MS - BUDDY_EVENT_MIN_MS));
  if ((int32_t)(nowMs - start) < 0) return b;
  uint32_t in = nowMs - start;
  if (in >= duration) return b;
  float t = (float)in / (float)duration;
  b.active = true;
  b.eventId = epoch;
  b.startMs = start;
  b.durationMs = duration;
  // Crawls in from one side and out the other, wandering up and down.
  float dir = ((h >> 16) & 1u) ? 1.0f : -1.0f;
  float from = dir > 0.0f ? -20.0f : 340.0f;
  float to = dir > 0.0f ? 340.0f : -20.0f;
  b.x = from + (to - from) * t;
  float wobble = sinf(t * 6.2831853f * 1.5f + (float)((h >> 20) % 100u));
  b.y = 96.0f + wobble * 34.0f - (float)in * 0.0015f;
  return b;
}

// Where the eyes should look to follow the bug: a bounded offset from the eye
// line, so the pair leans towards it without leaving the glass.
static inline void buddyBugGaze(const BuddyBug &b, float *gazeX, float *gazeY) {
  float gx = (b.x - 160.0f) / 9.0f;
  float gy = (b.y - 88.0f) / 12.0f;
  if (gx > 22.0f) gx = 22.0f;
  if (gx < -22.0f) gx = -22.0f;
  if (gy > 14.0f) gy = 14.0f;
  if (gy < -14.0f) gy = -14.0f;
  *gazeX = gx;
  *gazeY = gy;
}

// One Z of the sleeping animation: three letters rise and fade on a 2.4 s loop.
#define BUDDY_ZZZ_MS 2400u
#define BUDDY_ZZZ_COUNT 3

static inline void buddyZzz(uint32_t nowMs, int index, int *x, int *y,
                            int *alphaPercent) {
  if (index < 0) index = 0;
  if (index >= BUDDY_ZZZ_COUNT) index = BUDDY_ZZZ_COUNT - 1;
  float t = (float)(nowMs % BUDDY_ZZZ_MS) / (float)BUDDY_ZZZ_MS;
  float local = t + (float)index / (float)BUDDY_ZZZ_COUNT;
  local -= floorf(local);
  *x = 244 + (int)(local * 30.0f);
  *y = 112 - (int)(local * 54.0f) - index * 6;
  float fade = 1.0f - local;
  *alphaPercent = (int)(fade * 80.0f);
}

// Waking up. A shut eye is a different shape (a bar), not a lid, so the opening
// has to be manufactured: the renderer seeds the pose filter's lid to shut on the
// frame the face wakes (`buddyPoseFilterSeedLid`), the target here is an open eye
// with a startle, and the ease travels the lid from 1 to 0. That is the ~100 ms
// opening the owner asked for, instead of the shape popping open.
static inline void buddyWakeEyes(BuddyPose *p) {
  if (!p) return;
  p->closed = false;
  p->blink = false;
  p->happy = false;
  p->lid = 0.0f;
  p->lidL = 0.0f;
  p->lidR = 0.0f;
  p->surprise = true;
  p->rx = 50;
  p->ry = 60;
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
      // Confusion: eyes point at each other and blink on a fast, fixed beat.
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
    case BUDDY_ASLEEP:
      // `ageMs` is the time since the last IMU stimulus, so the bands are the
      // sleep thresholds in `buddy_sleep.h` and not arbitrary ones: light sleep
      // at 60 s, no more lids at 120 s, eyes shut at 180 s.
      if (ageMs < 60000) {
        // Just nodded off: still looks present, normal blink.
      } else if (ageMs < 120000) {
        // Light sleep: gaze drifts to the sides and comes back, with a sigh.
        p.gazeX = sinf((float)ageMs * 0.0009f) * 14.0f;
        p.lid = 0.08f;
      } else if (ageMs < 180000) {
        // Starts to nod: the lids drop and bounce.
        p.lid = 0.35f;
        p.gazeY = sinf((float)ageMs * 0.002f) * 3.0f;
      } else {
        // Deep sleep: the eyes shut and the face stops. No breath, no drift,
        // no blink. The owner asked for exactly that ("que ahi no pase nada");
        // motion is what wakes it, and now the bridge cannot argue.
        p.closed = true;
        p.bob = 0.0f;
        genericBlink = false;
        return p;
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
    // Idle life: a 1-3 px microsaccade every 2-5 s, the odd side glance, and a
    // breath that lifts the whole eye line about one pixel.
    BuddySaccade sc = buddySaccadeAt(nowMs, seed);
    BuddyGlance gl = buddyGlanceAt(nowMs, seed);
    float breath = buddyBreath(nowMs);
    p.gazeX += sc.x + gl.x;
    p.gazeY += sc.y + breath * 0.35f;
    p.bob = breath * 1.0f;
  }

  if (state == BUDDY_ASLEEP) {
    // The breath never stops, not even asleep: it is the only motion left.
    float breath = buddyBreath(nowMs);
    p.bob = p.closed ? breath * 2.0f : breath * 1.2f;
  }

  if (genericBlink && !p.blink && !p.happy && !p.closed && !p.dim) {
    // Not a timer: the blinks come unevenly, sometimes two in a row, sometimes
    // one that stays shut longer. See buddyBlinkAt.
    p.blink = buddyBlinkAt(nowMs, seed);
  }
  return p;
}
