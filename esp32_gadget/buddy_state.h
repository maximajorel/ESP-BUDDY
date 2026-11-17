// Sofía's face state and label rules.
//
// The bridge speaks six state names and a short human label. Pure and
// header-only so `tools/buddy_state_test.cpp` pins the mapping and the cleaning:
// an unknown state must still draw something, and a label with control bytes or
// five hundred characters must never run off the 320x172 panel.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BUDDY_LABEL_MAX 40

enum BuddyState {
  BUDDY_IDLE,
  BUDDY_BORED,        // idle for >180 s, the bridge says so
  BUDDY_THINKING,
  BUDDY_TOOL,
  BUDDY_WAITING,
  BUDDY_ERROR,
  BUDDY_DONE,
  BUDDY_OFFLINE,      // the bridge did not answer
  BUDDY_UNCONFIGURED  // no URL or no token yet
};

// Exact names from the bridge. Anything else falls back to idle: Sofía adds
// states without notice, and an unknown label must never freeze the face.
// Offline is reserved for a bridge that did not answer at all.
static inline BuddyState buddyStateFromName(const char *name) {
  if (!name || !*name) return BUDDY_IDLE;
  if (strcmp(name, "idle") == 0) return BUDDY_IDLE;
  if (strcmp(name, "bored") == 0) return BUDDY_BORED;
  if (strcmp(name, "thinking") == 0) return BUDDY_THINKING;
  if (strcmp(name, "tool") == 0) return BUDDY_TOOL;
  if (strcmp(name, "waiting") == 0) return BUDDY_WAITING;
  if (strcmp(name, "error") == 0) return BUDDY_ERROR;
  if (strcmp(name, "done") == 0) return BUDDY_DONE;
  return BUDDY_IDLE;
}

static inline const char *buddyStateWireName(BuddyState state) {
  switch (state) {
    case BUDDY_IDLE: return "idle";
    case BUDDY_BORED: return "bored";
    case BUDDY_THINKING: return "thinking";
    case BUDDY_TOOL: return "tool";
    case BUDDY_WAITING: return "waiting";
    case BUDDY_ERROR: return "error";
    case BUDDY_DONE: return "done";
    case BUDDY_UNCONFIGURED: return "unconfigured";
    case BUDDY_OFFLINE: break;
  }
  return "offline";
}

// Trims outer spaces, turns control bytes into spaces (they have no glyph and
// would punch holes in the label), and cuts at BUDDY_LABEL_MAX or the buffer.
// Always NUL-terminates and returns the length written.
static inline size_t buddyLabelClean(const char *in, char *out, size_t cap) {
  if (!out || cap == 0) return 0;
  out[0] = '\0';
  if (!in) return 0;

  size_t budget = cap - 1;
  if (budget > BUDDY_LABEL_MAX) budget = BUDDY_LABEL_MAX;
  if (budget == 0) return 0;

  while (*in == ' ') in++;
  size_t n = 0;
  for (const char *p = in; *p && n < budget; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c == 0x7F) c = ' ';
    out[n++] = (char)c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;
  out[n] = '\0';
  return n;
}
