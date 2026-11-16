// Debounce for a single active-low push button.
//
// Kept free of Arduino headers so the same code the firmware runs can also be
// compiled and exercised on a PC; the state machine is the part that is easy to
// get subtly wrong, and it should not need a finger on the board to test.

#pragma once

#include <stdint.h>

class ButtonDebounce {
 public:
  // Returns true exactly once for each accepted press.
  //
  // A press is accepted after the pin has stayed low for `stableMs`. Shorter
  // low pulses are treated as contact bounce. Holding the button keeps
  // producing nothing until it is released, so one press is one event.
  bool update(bool high, uint32_t now, uint32_t stableMs = 30) {
    if (high != prevHigh_) {
      prevHigh_ = high;
      changedAt_ = now;
      pending_ = !high;
      return false;
    }
    if (pending_ && !high && (uint32_t)(now - changedAt_) >= stableMs) {
      pending_ = false;
      return true;
    }
    return false;
  }

  bool isHigh() const { return prevHigh_; }

 private:
  bool prevHigh_ = true;   // BOOT is pulled up, so idle reads high
  bool pending_ = false;   // a low level is being timed
  uint32_t changedAt_ = 0;
};
