// Auto-rotation of the landscape image and the owner's lock.
//
// The driver can blit the 320x172 canvas onto the portrait glass in four ways, of
// which two are unmirrored and 180 degrees apart (see display.h). This module is
// the single place that decides which one is on the glass, so the CONFIG screen,
// the web panel and the console cannot disagree: every path goes through
// rotationSetMode/rotationSetLocked and every path repaints the same way.
//
// Three things can stop the sensor from acting on its own: the owner pinned an
// image, the owner locked the current one, or a game is in progress and the
// player is tilting the board on purpose.
#pragma once

#include <Arduino.h>

#include "rotation_policy.h"

// Reads the stored mode and lock, applies them, and seeds the policy with the
// rotation the owner saw last. Call once from setup(), before the first frame.
void rotationBegin();

// Follows the accelerometer when it is allowed to. Cheap and self-throttling:
// calling it from loop() costs one comparison most of the time.
void rotationTick(uint32_t nowMs);

// ROT_MODE_AUTO or an ORIENT_* value. Anything corrupt reads as auto.
uint8_t rotationMode();
bool rotationLocked();

// Owner-facing name of the current mode: "auto", "horizontal", "inverted", or
// "raw" for the debug values only the console can pin.
const char *rotationModeName();

// Orientation currently on the glass (an ORIENT_* value).
uint8_t rotationApplied();

// +1 when the sensor axes line up with the image the owner is looking at, -1
// when the image is the other one of the pair. The game's steering and Sofia's
// face multiply their in-plane readings by this so "left" stays the left of what
// is on the glass.
int8_t rotationInPlaneSign();

// Applies and persists. A mode the firmware does not know is refused and leaves
// everything as it was.
bool rotationSetMode(uint8_t mode);

// Persists the lock and, when locking, freezes whatever is on the glass now.
bool rotationSetLocked(bool locked);

// While held the image never changes on its own. The game raises this for as long
// as it is playing or calibrating, because a repaint mid-run would be a visible
// stumble for a decision the player is not asking for.
void rotationSetHeld(bool held);
bool rotationIsHeld();
