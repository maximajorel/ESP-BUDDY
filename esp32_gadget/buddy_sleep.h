// When does the face sleep? The board decides, the bridge only reports.
//
// The bridge answers `GET /state` with a state, a label and `sleep_hint`: how
// long Hermes has been quiet. That number is an input, not an order. Sleeping is
// patience, and patience belongs to the body that can feel a shake, so the rule
// is the MIN of the server's silence and the board's own silence:
//
//     silencio_servidor = sleep_hint
//     silencio_local    = now - last IMU stimulus
//     inactividad       = MIN(silencio_servidor, silencio_local)
//
// The MIN is the whole fix for "lo muevo y se despierta, lo dejo y se vuelve a
// dormir al toque": a bridge that has been quiet for minutes can no longer put a
// board to sleep that was shaken ten seconds ago. It also means a busy server
// (a low hint) keeps the face awake even when the desk is still, because a body
// at rest is not a body that is alone.
//
// Pure and header-only so `tools/buddy_sleep_test.cpp` pins the thresholds and
// the wrap. The ladders themselves live in `buddy_anim.h`.
#pragma once

#include <stdint.h>

// Seconds of inactivity before the lids start to drop, and before they close.
// Starting points from the bridge contract, adjustable without touching the rule.
#ifndef BUDDY_SLEEP_LIGHT_SEC
#define BUDDY_SLEEP_LIGHT_SEC 60
#endif
#ifndef BUDDY_SLEEP_DEEP_SEC
#define BUDDY_SLEEP_DEEP_SEC 180
#endif

// Below this the server is working, so it is not patience, it is a busy spell.
// Out of the published table: `sleep_hint` under five seconds means Hermes just
// answered something.
#define BUDDY_SLEEP_HINT_BUSY_SEC 5

enum BuddySleepStage {
  BUDDY_SLEEP_AWAKE = 0,
  BUDDY_SLEEP_LIGHT = 1,  // cabeceo: lids heavy, still present
  BUDDY_SLEEP_DEEP = 2    // eyes shut, only the breath moves
};

// The two halves of the MIN. `hintValid` is false for a bridge that predates the
// field: then the only silence that can be measured is the board's own.
struct BuddySleepFacts {
  float hintSec = 0.0f;
  bool hintValid = false;
  uint32_t localIdleMs = 0;
};

// The inactivity the decision is made on, in milliseconds.
static inline uint32_t buddySleepInactiveMs(const BuddySleepFacts &f) {
  uint32_t local = f.localIdleMs;
  if (!f.hintValid) return local;
  // A negative or non-finite hint is a broken field, not a busy server: read it
  // as zero so the face stays awake rather than sleeping on a nonsense value.
  if (!(f.hintSec >= 0.0f)) return 0;
  float serverMs = f.hintSec * 1000.0f;
  uint32_t server = serverMs > 0.0f ? (uint32_t)serverMs : 0u;
  if (serverMs > 4294967040.0f) server = 4294967040u;
  return server < local ? server : local;
}

static inline BuddySleepStage buddySleepStage(const BuddySleepFacts &f) {
  // The busy check first: `sleep_hint` under the threshold is the server talking
  // to us right now, and no amount of local quiet outranks that.
  if (f.hintValid && f.hintSec < (float)BUDDY_SLEEP_HINT_BUSY_SEC) {
    return BUDDY_SLEEP_AWAKE;
  }
  uint32_t inactive = buddySleepInactiveMs(f);
  if (inactive >= (uint32_t)BUDDY_SLEEP_DEEP_SEC * 1000u) return BUDDY_SLEEP_DEEP;
  if (inactive >= (uint32_t)BUDDY_SLEEP_LIGHT_SEC * 1000u) return BUDDY_SLEEP_LIGHT;
  return BUDDY_SLEEP_AWAKE;
}

// The board's own half: the last time the IMU felt anything. Zero-initialised
// means "moved at boot", so the first idle stretch is measured from power-on
// instead of waiting for a shake that may never come.
struct BuddySleepClock {
  uint32_t lastStimulusMs = 0;
};

// Any real movement calls this. It never waits for the bridge: that is why a
// shake wakes the face even when the next poll would still say `bored`.
static inline void buddySleepStimulus(BuddySleepClock *c, uint32_t nowMs) {
  c->lastStimulusMs = nowMs;
}

// Milliseconds since the last stimulus. A stamp a few ms ahead of the clock is
// not a wrap, it is the frame reading one millis() later than the event did;
// the unsigned subtraction would call that ~49 days idle and sleep on the spot.
static inline uint32_t buddySleepClockIdleMs(const BuddySleepClock *c,
                                             uint32_t nowMs) {
  if ((int32_t)(nowMs - c->lastStimulusMs) < 0) return 0;
  return nowMs - c->lastStimulusMs;
}

static inline BuddySleepStage buddySleepClockStage(const BuddySleepClock *c,
                                                   uint32_t nowMs, float hintSec,
                                                   bool hintValid) {
  BuddySleepFacts f;
  f.hintSec = hintSec;
  f.hintValid = hintValid;
  f.localIdleMs = buddySleepClockIdleMs(c, nowMs);
  return buddySleepStage(f);
}
