// One study countdown for the whole firmware.
//
// Concentración is started from the public web page, advanced once per loop and
// drawn by the face, which are three different translation units. The state is
// `buddy_focus.h` (pure and host-tested); this header declares the one owner of
// the live instance and the four calls those units need.
//
// The instance lives in `buddy_focus_state.cpp`, not in an inline function in
// this header: with a header-only inline static, three translation units were
// supposed to share one object through COMDAT merging and on the board they did
// not, which is how the first real session ran with the glass showing nothing.
// One definition in one file cannot be merged wrong.
//
// No lock: the HTTP handler, the per-loop advance and the draw tick all run
// inside loop(), on the same task. The bridge poller task never looks at it. If a
// future change moves the handler to another task, this is where a mutex goes.
#pragma once

#include <stdint.h>

#include "buddy_focus.h"

// Starts (or replaces) the countdown with an exact number of seconds; zero
// cancels. The board's `nowMs` comes from millis().
void buddyFocusRequestSeconds(uint32_t seconds, uint32_t nowMs);

// Starts (or replaces) the countdown in whole minutes; zero cancels. Kept for
// callers that speak minutes (the console, the old API field).
void buddyFocusRequest(uint16_t minutes, uint32_t nowMs);

// The live countdown. Callers that only read must not write to it.
const BuddyFocus &buddyFocusView();

// Runs once per loop, whatever screen is visible, so a session ends on time even
// when the glass is showing something else. True on the single tick where it
// reached zero: that is where the bead gets its green.
bool buddyFocusAdvance(uint32_t nowMs);

// The dark button can put the glass to sleep mid-session (screens.cpp). The
// timer keeps running either way — the deadline is absolute — but a study session
// should hold the screen awake.
bool buddyFocusHoldsScreenOn(uint32_t nowMs);
