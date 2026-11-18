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
  BUDDY_GAME_HAPPY,     // you won, she takes it well
  BUDDY_GAME_ANGER,     // she won
  BUDDY_GAME_SLEEPY,    // a draw
  BUDDY_GAME_SAD,       // she is losing
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
};

static inline void buddyGameSet(BuddyGameOverride *o, BuddyGameMood mood,
                                uint32_t holdMs, uint32_t nowMs) {
  if (!o) return;
  o->mood = mood;
  o->untilMs = nowMs + buddyGameClampMs(holdMs);
}

static inline void buddyGameClear(BuddyGameOverride *o) {
  if (!o) return;
  o->mood = BUDDY_GAME_NONE;
  o->untilMs = 0;
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

// Applies one mood to the pose the state produced. Every mood starts from an
// open, neutral eye so nothing from the previous expression survives.
static inline void buddyGameApplyPose(BuddyPose *p, BuddyGameMood mood) {
  if (!p || mood == BUDDY_GAME_NONE) return;

  p->closed = false;
  p->blink = false;
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
    case BUDDY_GAME_SAD:
      // Tired: the top lid droops and the bottom cuts the eye.
      p->lid = 0.22f;
      p->bottom = 0.45f;
      break;
    case BUDDY_GAME_NONE:
      break;
  }
}
