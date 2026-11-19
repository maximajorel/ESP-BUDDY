// The bridge's `phase` and its LED colour, pure and header-only.
//
// Sofía's second doc separates the face (emotion) from a small HUD (functional
// state). The bridge sends `phase` already resolved (busy|ok|ask|idle) and a
// `led` colour, so the firmware must not derive the phase from `state` and must
// not translate colours: an unknown phase is idle, never a false check.
#pragma once

#include <stdint.h>
#include <string.h>

enum BuddyPhase {
  BUDDY_PHASE_IDLE,
  BUDDY_PHASE_BUSY,
  BUDDY_PHASE_OK,
  BUDDY_PHASE_ASK,
  // An appointment is close. Its own phase (not `ask`) because the LED needs a
  // colour that is neither "I need you now" red nor "I am working" amber, and
  // because the bead pulses it instead of holding it flat.
  BUDDY_PHASE_EVENT,
};

static inline BuddyPhase buddyPhaseFromName(const char *name) {
  if (!name) return BUDDY_PHASE_IDLE;
  if (strcmp(name, "busy") == 0) return BUDDY_PHASE_BUSY;
  if (strcmp(name, "ok") == 0) return BUDDY_PHASE_OK;
  if (strcmp(name, "ask") == 0) return BUDDY_PHASE_ASK;
  if (strcmp(name, "event") == 0) return BUDDY_PHASE_EVENT;
  // "idle", an unknown future phase, or a missing field: the HUD stays empty.
  return BUDDY_PHASE_IDLE;
}

static inline const char *buddyPhaseWireName(BuddyPhase phase) {
  switch (phase) {
    case BUDDY_PHASE_BUSY: return "busy";
    case BUDDY_PHASE_OK: return "ok";
    case BUDDY_PHASE_ASK: return "ask";
    case BUDDY_PHASE_EVENT: return "event";
    case BUDDY_PHASE_IDLE: break;
  }
  return "idle";
}

struct BuddyRgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Used when the bridge's `led` array is missing or malformed. The values match
// Sofía's table so the firmware still behaves if the field is added later.
static inline BuddyRgb buddyPhaseDefaultLed(BuddyPhase phase) {
  switch (phase) {
    case BUDDY_PHASE_BUSY: return {255, 190, 0};
    case BUDDY_PHASE_OK: return {0, 255, 80};
    case BUDDY_PHASE_ASK: return {255, 40, 40};
    // Violet: nothing else in the table is violet, so "the clock is talking"
    // can never be mistaken for working, done or a question.
    case BUDDY_PHASE_EVENT: return {170, 80, 255};
    case BUDDY_PHASE_IDLE: break;
  }
  return {0, 180, 255};
}

// The bridge only sends `ok` for /buddy/ask runs. Desktop and Telegram go from
// `busy` straight to `idle`; that transition is the completion signal the
// firmware has to synthesize (green blink + check). Ask is never a finish.
static inline bool buddyPhaseFinished(BuddyPhase previous, BuddyPhase now) {
  return now == BUDDY_PHASE_OK ||
         (previous == BUDDY_PHASE_BUSY && now == BUDDY_PHASE_IDLE);
}

// How long a fabricated check stays on the glass before the HUD returns to the
// real phase.
#define BUDDY_HUD_SYNTH_OK_MS 3000u

// The HUD is a transition, not a state. `ok` pops once when it arrives, and
// when the bridge skips it (the Desktop/Telegram path) the drop from busy to
// idle fabricates it. The previous phase is remembered, because without that
// guard the check would fire again on every idle poll and the twinkle would
// mean nothing.
struct BuddyHud {
  bool have = false;
  BuddyPhase phase = BUDDY_PHASE_IDLE;  // the last phase the bridge reported
  BuddyPhase shown = BUDDY_PHASE_IDLE;  // what the glass should draw
  uint32_t checkAt = 0;                 // when the current check started
  bool sticky = false;                  // the bridge itself still says ok
};

static inline void buddyHudStep(BuddyHud *h, BuddyPhase phase,
                                uint32_t nowMs) {
  if (!h->have) {
    h->have = true;
    h->phase = phase;
    h->shown = phase;
    if (phase == BUDDY_PHASE_OK) {
      h->checkAt = nowMs;
      h->sticky = true;
    }
    return;
  }
  if (phase == h->phase) return;
  if (phase == BUDDY_PHASE_OK) {
    h->checkAt = nowMs;
    h->sticky = true;
    h->shown = BUDDY_PHASE_OK;
  } else if (buddyPhaseFinished(h->phase, phase)) {
    // Desktop/Telegram never send `ok`: the drop from busy to idle is the cue.
    h->checkAt = nowMs;
    h->sticky = false;
    h->shown = BUDDY_PHASE_OK;
  } else {
    h->sticky = false;
    h->shown = phase;
  }
  h->phase = phase;
}

// What to draw right now: the sticky/fabricated check while it lasts, then the
// real phase.
static inline BuddyPhase buddyHudShown(const BuddyHud *h, uint32_t nowMs) {
  if (h->shown != BUDDY_PHASE_OK) return h->shown;
  if (h->sticky) return BUDDY_PHASE_OK;
  if ((int32_t)(h->checkAt + BUDDY_HUD_SYNTH_OK_MS - nowMs) > 0) {
    return BUDDY_PHASE_OK;
  }
  return h->phase;
}

// Milliseconds since the current check started, for its pop and twinkle.
// Meaningless unless buddyHudShown() is BUDDY_PHASE_OK.
static inline uint32_t buddyHudCheckAge(const BuddyHud *h, uint32_t nowMs) {
  return nowMs - h->checkAt;
}

// The eyes' cyan, shared so the LED and the face read as one thing.
static inline BuddyRgb buddyEyesCyan() { return {0, 180, 255}; }
