// The bead as Sofía's functional status light (second doc, LED section).
//
// The bridge sends `led` already resolved; this module only adds the timing the
// server cannot: the green 5 s blink on `ok` (and synthesized on busy -> idle),
// the fade back to cyan, and the slow red pulse for `ask`. It runs every loop,
// not only on the buddy screen, because the whole point is seeing it from afar.
#pragma once

#include <stdint.h>

void buddyLedTick(uint32_t nowMs);
