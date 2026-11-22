// The transpose that sends the 320x172 landscape framebuffer to the portrait
// panel, pure and host-testable.
//
// `displayPush` runs 55 040 times per frame inside a nested loop, and the four
// orientations were being chosen with a `switch` *per pixel*. Hoisting the switch
// out of the loop (one loop per orientation) is what makes a higher frame rate
// affordable at all, so the mapping itself lives here, pinned by
// `tools/display_blit_test.cpp`: a wrong mapping shears the whole image and the
// panel has no MISO to read back.
#pragma once

#include <stdint.h>

// The same numbers as display.h, which cannot be included here (it pulls in
// Arduino.h and this header is host-buildable). display.cpp pins the agreement
// with a static_assert, so the two can never drift: an unknown value must behave
// exactly like the default rather than writing outside the panel.
#define BLIT_ORIENT_MIRROR 0
#define BLIT_ORIENT_CW 1
#define BLIT_ORIENT_CCW 2
#define BLIT_ORIENT_MIRROR2 3

// Where pixel (x, y) of the width x height framebuffer lands on the panel.
static inline void displayBlitTarget(int orientation, int x, int y, int w,
                                     int panelW, int *px, int *py) {
  int outX, outY;
  switch (orientation) {
    case BLIT_ORIENT_MIRROR:
      outX = y;
      outY = x;
      break;
    case BLIT_ORIENT_CCW:
      outX = y;
      outY = w - 1 - x;
      break;
    case BLIT_ORIENT_MIRROR2:
      outX = panelW - 1 - y;
      outY = w - 1 - x;
      break;
    case BLIT_ORIENT_CW:
    default:
      outX = panelW - 1 - y;
      outY = x;
      break;
  }
  if (px) *px = outX;
  if (py) *py = outY;
}
