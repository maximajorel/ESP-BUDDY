// The web game's mood on the face, pure and header-only.
//
// The ta-te-ti runs in the browser, but the eyes that react are the device's.
// The page cannot see the bridge, the IMU or the state ladder, so it may not
// describe a pose: it names a mood from a closed vocabulary, and this module
// turns that name into a short-lived overlay on whatever the face is doing.
//
// Two rules keep the overlay honest and host-tested (`tools/buddy_game_test.cpp`):
//
// - it always expires, so a closed tab can never freeze the face in an
//   expression the bridge did not choose;
// - each mood owns every channel that makes it readable (lids, hot tint, joy,
//   bottom lid) and clears the ones it does not, so the state's previous
//   expression does not leak into the game's reaction.
//
// While the overlay is active it wins over the bridge pose: it is applied to the
// pose the state produced, before the easing, so a reaction travels there
// smoothly instead of popping.
#pragma once

#include <stdint.h>
#include <string.h>

#include "buddy_anim.h"

// Long enough to read a reaction, short enough that the face is never stuck:
// the page refreshes the mood on every move it cares about.
#define BUDDY_GAME_DEFAULT_MS 2500u
#define BUDDY_GAME_MIN_MS 400u
#define BUDDY_GAME_MAX_MS 8000u

enum BuddyGameMood {
  BUDDY_GAME_NONE,
  BUDDY_GAME_FOCUS,     // your turn is being considered
  BUDDY_GAME_THINKING,  // one eye narrowed: she is choosing a cell
  BUDDY_GAME_ALERT,     // she saw your threat
  BUDDY_GAME_HAPPY,     // she won
  BUDDY_GAME_ANGER,     // not the game's: the bridge anger, or the panel's test chip
  BUDDY_GAME_SLEEPY,    // a draw
  BUDDY_GAME_SAD,       // she lost
};

// An unknown, empty or null name is not an error: it clears the overlay, because
// the safe resting state is always the bridge's face.
static inline BuddyGameMood buddyGameMoodFromName(const char *name,
                                                  BuddyGameMood fallback) {
  if (!name || !name[0]) return BUDDY_GAME_NONE;
  if (!strcmp(name, "focus")) return BUDDY_GAME_FOCUS;
  if (!strcmp(name, "thinking")) return BUDDY_GAME_THINKING;
  if (!strcmp(name, "alert")) return BUDDY_GAME_ALERT;
  if (!strcmp(name, "happy")) return BUDDY_GAME_HAPPY;
  if (!strcmp(name, "anger")) return BUDDY_GAME_ANGER;
  if (!strcmp(name, "sleepy")) return BUDDY_GAME_SLEEPY;
  if (!strcmp(name, "sad")) return BUDDY_GAME_SAD;
  (void)fallback;
  return BUDDY_GAME_NONE;
}

static inline const char *buddyGameMoodWireName(BuddyGameMood mood) {
  switch (mood) {
    case BUDDY_GAME_FOCUS: return "focus";
    case BUDDY_GAME_THINKING: return "thinking";
    case BUDDY_GAME_ALERT: return "alert";
    case BUDDY_GAME_HAPPY: return "happy";
    case BUDDY_GAME_ANGER: return "anger";
    case BUDDY_GAME_SLEEPY: return "sleepy";
    case BUDDY_GAME_SAD: return "sad";
    case BUDDY_GAME_NONE: break;
  }
  return "";
}

// Zero means "use the default"; anything else is held to the window the glass
// can actually show.
static inline uint32_t buddyGameClampMs(uint32_t holdMs) {
  if (holdMs == 0u) return BUDDY_GAME_DEFAULT_MS;
  if (holdMs < BUDDY_GAME_MIN_MS) return BUDDY_GAME_MIN_MS;
  if (holdMs > BUDDY_GAME_MAX_MS) return BUDDY_GAME_MAX_MS;
  return holdMs;
}

struct BuddyGameOverride {
  BuddyGameMood mood = BUDDY_GAME_NONE;
  uint32_t untilMs = 0;
  // The mood's own clock, for the gestures that are animation and not pose.
  uint32_t startMs = 0;
};

static inline void buddyGameSet(BuddyGameOverride *o, BuddyGameMood mood,
                                uint32_t holdMs, uint32_t nowMs) {
  if (!o) return;
  o->mood = mood;
  o->untilMs = nowMs + buddyGameClampMs(holdMs);
  o->startMs = nowMs;
}

static inline void buddyGameClear(BuddyGameOverride *o) {
  if (!o) return;
  o->mood = BUDDY_GAME_NONE;
  o->untilMs = 0;
  o->startMs = 0;
}

// Wrap-safe: the same subtraction the rest of the firmware uses for deadlines,
// so a millis() rollover neither extends nor cancels a reaction.
static inline bool buddyGameActive(const BuddyGameOverride *o, uint32_t nowMs) {
  if (!o || o->mood == BUDDY_GAME_NONE) return false;
  return (int32_t)(o->untilMs - nowMs) > 0;
}

static inline BuddyGameMood buddyGameCurrent(const BuddyGameOverride *o,
                                             uint32_t nowMs) {
  return buddyGameActive(o, nowMs) ? o->mood : BUDDY_GAME_NONE;
}

// How far into the mood we are, wrap-safe like every other deadline in the
// firmware. A clock that appears to sit before the start reads as zero instead
// of a huge unsigned age.
static inline uint32_t buddyGameElapsed(const BuddyGameOverride *o,
                                        uint32_t nowMs) {
  if (!o || o->mood == BUDDY_GAME_NONE) return 0;
  int32_t dt = (int32_t)(nowMs - o->startMs);
  return dt > 0 ? (uint32_t)dt : 0u;
}

// --- the sad gesture ---------------------------------------------------------
//
// Sad is the mirror of anger, not a squint: the outer corners fall (rotL -7,
// rotR +7) while the lid barely lowers. The even lid of focus is what this must
// not look like, so the resting lid stays under it; the gesture lives in the
// gaze and in a slow sigh that never closes the eye.
//
// The artboard runs for 11 s; the game hold is 6 s, so the same beats are packed
// into this table. The sigh is deliberately capped below a full lid: on this
// panel a shut eye is a 4 px bar, and a bar held for a sigh reads as a line, not
// as fatigue. The "no" is animation, not pose, which is why it lives here and
// not in the switch.
struct BuddyGameSadFrame {
  float lid;
  float gazeX;
  float gazeY;
  float turn;
};

struct BuddyGameSadKey {
  uint16_t atMs;
  float lid;
  float gazeX;
  float gazeY;
  float turn;
};

static const BuddyGameSadKey kBuddyGameSadKeys[] = {
    // Hold: already sad, looking down and a little aside.
    {0u, 0.14f, -3.0f, 2.5f, 0.0f},
    {2400u, 0.14f, -3.0f, 2.5f, 0.0f},
    // The sigh: a heavy lid for about a second, never a close.
    {2600u, 0.36f, -3.0f, 2.5f, 0.0f},
    {2750u, 0.62f, -3.0f, 2.5f, 0.0f},
    {3350u, 0.62f, -3.0f, 2.5f, 0.0f},
    {3650u, 0.45f, -3.0f, 3.0f, 0.0f},
    // Opens heavy, and the gaze stays a pixel lower than it began.
    {4000u, 0.14f, -4.0f, 3.5f, 0.0f},
    {4400u, 0.14f, -4.0f, 3.5f, 0.0f},
    // Three small "no"s: the gaze and the head disagree on purpose, each turn
    // is slower than a reflex, and the last one gives up instead of restarting.
    {4600u, 0.14f, 3.0f, 3.5f, 0.09f},
    {4800u, 0.14f, -6.0f, 3.5f, -0.11f},
    {5000u, 0.14f, 3.5f, 3.5f, 0.09f},
    {5200u, 0.14f, -6.0f, 3.5f, -0.11f},
    {5400u, 0.14f, 2.0f, 3.5f, 0.05f},
    {5600u, 0.14f, -4.0f, 3.5f, -0.04f},
    // Settles back to the resting sad pose, so an 8 s hold does not freeze a beat.
    {5800u, 0.14f, -3.0f, 3.5f, 0.0f},
    {6000u, 0.14f, -3.0f, 2.5f, 0.0f},
};

static inline BuddyGameSadFrame buddyGameSadAt(uint32_t elapsedMs) {
  const uint32_t n =
      (uint32_t)(sizeof(kBuddyGameSadKeys) / sizeof(kBuddyGameSadKeys[0]));
  const BuddyGameSadKey &first = kBuddyGameSadKeys[0];
  if (elapsedMs <= first.atMs) {
    return {first.lid, first.gazeX, first.gazeY, first.turn};
  }
  for (uint32_t i = 1; i < n; i++) {
    const BuddyGameSadKey &a = kBuddyGameSadKeys[i - 1];
    const BuddyGameSadKey &b = kBuddyGameSadKeys[i];
    if (elapsedMs <= b.atMs) {
      float k = (float)(elapsedMs - a.atMs) / (float)(b.atMs - a.atMs);
      return {a.lid + (b.lid - a.lid) * k,
              a.gazeX + (b.gazeX - a.gazeX) * k,
              a.gazeY + (b.gazeY - a.gazeY) * k,
              a.turn + (b.turn - a.turn) * k};
    }
  }
  const BuddyGameSadKey &last = kBuddyGameSadKeys[n - 1];
  return {last.lid, last.gazeX, last.gazeY, last.turn};
}

// Applies one mood to the pose the state produced. Every mood starts from an
// open, neutral eye so nothing from the previous expression survives.
static inline void buddyGameApplyPose(BuddyPose *p, BuddyGameMood mood,
                                      uint32_t elapsedMs = 0u) {
  if (!p || mood == BUDDY_GAME_NONE) return;

  p->closed = false;
  p->blink = false;
  // The unpaired face is dim by design; a game reaction is not a dim face.
  p->dim = false;
  p->surprise = false;
  p->happy = false;
  p->hot = 0.0f;
  p->grow = 1.0f;
  p->bottom = 0.0f;
  p->lidL = 0.0f;
  p->lidR = 0.0f;
  p->rot = 0.0f;
  p->rotL = 0.0f;
  p->rotR = 0.0f;
  p->rx = 48;
  p->ry = 48;

  switch (mood) {
    case BUDDY_GAME_FOCUS:
      p->lid = 0.18f;
      break;
    case BUDDY_GAME_THINKING:
      // The same asymmetric squint the state ladder uses for "thinking".
      p->lid = 0.0f;
      p->lidL = 0.04f;
      p->lidR = 0.42f;
      break;
    case BUDDY_GAME_ALERT:
      // Wider than open: the eye grows a touch and the lid disappears.
      p->lid = 0.02f;
      p->rx = 52;
      p->ry = 52;
      break;
    case BUDDY_GAME_HAPPY:
      // Joy is the lower lid, never a brow.
      p->lid = 0.0f;
      p->happy = true;
      break;
    case BUDDY_GAME_ANGER:
      // The anatomy the IMU uses for anger: hot tint, bigger eye, angled lids.
      p->lid = 0.0f;
      p->hot = 1.0f;
      p->grow = 1.12f;
      p->rotL = 8.0f;
      p->rotR = -8.0f;
      break;
    case BUDDY_GAME_SLEEPY:
      p->lid = 1.0f;
      p->closed = true;
      break;
    case BUDDY_GAME_SAD: {
      // The eye that falls outward. The gesture is time-shaped, so it reads the
      // elapsed clock instead of carrying the previous mood's channels.
      BuddyGameSadFrame f = buddyGameSadAt(elapsedMs);
      p->lid = f.lid;
      p->bottom = 0.10f;
      p->rotL = -7.0f;
      p->rotR = 7.0f;
      p->rx = 48;
      p->ry = 48;
      p->gazeX = f.gazeX;
      p->gazeY = f.gazeY;
      p->turn = f.turn;
      p->tilt = 0.0f;
      // The sigh covers most of the eye, never enough for buddyEyeHalfH to
      // collapse it to the 4 px blink bar or to leave only the glow thread.
      p->open = 1.0f;
      break;
    }
    case BUDDY_GAME_NONE:
      break;
  }
}
