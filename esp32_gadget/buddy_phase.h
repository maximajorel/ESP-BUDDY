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
};

static inline BuddyPhase buddyPhaseFromName(const char *name) {
  if (!name) return BUDDY_PHASE_IDLE;
  if (strcmp(name, "busy") == 0) return BUDDY_PHASE_BUSY;
  if (strcmp(name, "ok") == 0) return BUDDY_PHASE_OK;
  if (strcmp(name, "ask") == 0) return BUDDY_PHASE_ASK;
  // "idle", an unknown future phase, or a missing field: the HUD stays empty.
  return BUDDY_PHASE_IDLE;
}

static inline const char *buddyPhaseWireName(BuddyPhase phase) {
  switch (phase) {
    case BUDDY_PHASE_BUSY: return "busy";
    case BUDDY_PHASE_OK: return "ok";
    case BUDDY_PHASE_ASK: return "ask";
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

// The eyes' cyan, shared so the LED and the face read as one thing.
static inline BuddyRgb buddyEyesCyan() { return {0, 180, 255}; }
