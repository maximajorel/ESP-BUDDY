#include "buddy_focus_state.h"

// The single live countdown. Static, in one translation unit, so the HTTP
// handler, the per-loop advance and the draw tick can only ever see this object.
static BuddyFocus s_focus;

void buddyFocusRequest(uint16_t minutes, uint32_t nowMs) {
  buddyFocusStart(&s_focus, nowMs, minutes);
}

const BuddyFocus &buddyFocusView() { return s_focus; }

bool buddyFocusAdvance(uint32_t nowMs) {
  const bool wasActive = s_focus.active;
  buddyFocusTick(&s_focus, nowMs);
  return wasActive && !s_focus.active;
}

bool buddyFocusHoldsScreenOn(uint32_t nowMs) {
  return s_focus.active || buddyFocusDoneVisible(&s_focus, nowMs);
}
