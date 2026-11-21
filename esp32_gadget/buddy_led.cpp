// Sofía's rear bead: the colour the WS2812 should show.
//
// The WS2812 cannot be read back, so the decision left this file: the whole
// envelope (the finish twinkle, its guard, the fade, the mood colours) lives in
// `buddy_bead.h`, pure and pinned by `tools/buddy_bead_test.cpp`. This file only
// asks the view what the bridge and the face say, then paints the answer.
#include "buddy_led.h"

#include "buddy_bead.h"
#include "buddy_phase.h"
#include "rgb.h"
#include "screen_buddy.h"

static BuddyBead s_bead;

void buddyLedTick(uint32_t nowMs) {
  BuddyView view;
  buddyGetView(&view);
  // Without a bridge the RGB card owns the bead, unless the local calendar has
  // something to say: the event does not depend on the bridge any more.
  if (!view.configured && !view.calendarActive) return;

  // The buddy owns the bead while it is configured; a leftover rainbow effect
  // would hide the phase colour.
  rgbSetEffect(RGB_EFFECT_SOLID);

  if (!view.configured) {
    // Calendar only: breathe the event violet, because there is no bridge phase
    // to paint and a flat colour would read as a lamp left on.
    const float pulse = 0.45f + 0.55f * (0.5f + 0.5f * cosf((float)nowMs * 0.0026f));
    const BuddyRgb c = buddyPhaseDefaultLed(BUDDY_PHASE_EVENT);
    rgbSetColor((uint8_t)(c.r * pulse), (uint8_t)(c.g * pulse),
                (uint8_t)(c.b * pulse));
    return;
  }

  BuddyRgb c = buddyBeadTick(&s_bead, view.phase, view.led, view.ledValid,
                             buddyMood(), nowMs);
  rgbSetColor(c.r, c.g, c.b);
}
