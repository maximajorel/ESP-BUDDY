// One study countdown for the whole firmware.
//
// Concentración is started from the public web page and drawn by the face loop,
// which live in different translation units (net.cpp and screen_buddy.cpp). The
// state itself is `buddy_focus.h`, pure and tested on the host; this header is
// only the single shared instance plus the two calls those units need.
//
// No lock: both the HTTP handler and the draw tick run inside loop(), on the same
// task. The bridge poller task never looks at it. If a future change moves the
// handler to another task, this is the place that grows a mutex.
#pragma once

#include <stdint.h>

#include "buddy_focus.h"

inline BuddyFocus &buddyFocusShared() {
  static BuddyFocus focus;
  return focus;
}

// Starts (or replaces) the countdown; zero cancels. Returns false only when the
// pointer would be null, which cannot happen here, so callers may ignore it.
inline bool buddyFocusRequest(uint16_t minutes, uint32_t nowMs) {
  return buddyFocusStart(&buddyFocusShared(), nowMs, minutes);
}

// The dark button can put the glass to sleep mid-session (screens.cpp). The
// timer keeps running either way — the deadline is absolute — but the panel and
// the owner both read better when the study session holds the screen awake.
inline bool buddyFocusHoldsScreenOn(uint32_t nowMs) {
  const BuddyFocus &f = buddyFocusShared();
  return f.active || buddyFocusDoneVisible(&f, nowMs);
}

// Runs once per loop, whatever screen is visible, so a session ends on time even
// when the glass is showing something else. True on the single tick where it
// reached zero: that is where the bead gets its green, so the acknowledgement
// does not depend on the owner coming back to the SOFIA screen.
inline bool buddyFocusAdvance(uint32_t nowMs) {
  BuddyFocus &f = buddyFocusShared();
  const bool wasActive = f.active;
  buddyFocusTick(&f, nowMs);
  return wasActive && !f.active;
}
