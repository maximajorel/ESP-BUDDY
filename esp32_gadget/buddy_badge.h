// What the corner says when there is no bridge to draw a face for.
//
// Sofía's face is a readout of what Hermes is doing. When the link cannot carry
// that answer, dim eyes alone read as "she is bored" instead of "the connection is
// broken", which is the one thing the owner can act on. The HUD corner carries the
// mark; the panel says which cause it was.
//
// Pure and header-only so `tools/buddy_badge_test.cpp` pins the mapping.
#pragma once

#include "buddy_link.h"

// Whether the corner must show the no-link mark. The face stays either way (the
// owner asked for that in L19); this decides only which glyph owns the corner.
// Unconfigured is deliberately not a failure: the dim, calm face is the designed
// answer to "nobody paired me yet", and an alert icon there would read as damage
// instead of a pending setup. Every other cause means the bridge was expected and
// did not answer.
static inline bool buddyShowsLinkBadge(BuddyLink link) {
  if (link == BUDDY_LINK_OK) return false;
  if (link == BUDDY_LINK_UNCONFIGURED) return false;
  if (link < BUDDY_LINK_OK || link >= BUDDY_LINK_COUNT) return false;
  return true;
}
