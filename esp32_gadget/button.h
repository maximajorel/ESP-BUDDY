// Single-button gesture recogniser for the BOOT key on GPIO0.
//
// The board has exactly one usable button, so every screen shares the same two
// gestures: a short press interacts with the current screen and a three-second
// hold moves to the next one. The state machine is kept free of hardware access
// - update() takes the raw pin level and the current time - so it can be driven
// by a host test or by the serial console without a finger on the board.
#pragma once

#include <Arduino.h>

#define BTN_PIN 0
#define BTN_DEBOUNCE_MS 20
// One second, not three: a hold now opens the screen menu, and a second hold
// enters the highlighted screen. Three seconds made switching feel like waiting.
#define BTN_LONG_PRESS_MS 1000

struct ButtonEvent {
  bool pressed;     // press edge accepted after debounce
  bool released;    // release edge
  bool shortPress;  // released before the long-press threshold
  bool longPress;   // fired once, at the moment the hold crosses the threshold
};

class ButtonFsm {
 public:
  void reset() {
    s_down = false;
    s_rawLast = false;
    s_rawSince = 0;
    s_downSince = 0;
    s_longFired = false;
  }

  // `rawDown` true means the key is electrically pressed (pin reads LOW).
  ButtonEvent update(bool rawDown, uint32_t nowMs) {
    ButtonEvent ev = {false, false, false, false};

    if (rawDown != s_rawLast) {
      s_rawLast = rawDown;
      s_rawSince = nowMs;
    }

    bool stable = (uint32_t)(nowMs - s_rawSince) >= BTN_DEBOUNCE_MS;
    if (stable && s_rawLast != s_down) {
      s_down = s_rawLast;
      if (s_down) {
        ev.pressed = true;
        s_downSince = nowMs;
        s_longFired = false;
      } else {
        ev.released = true;
        if (!s_longFired) ev.shortPress = true;
      }
    }

    // The long press fires while the key is still held, so the screen changes
    // the instant the threshold is crossed instead of on release.
    if (s_down && !s_longFired &&
        (uint32_t)(nowMs - s_downSince) >= BTN_LONG_PRESS_MS) {
      s_longFired = true;
      ev.longPress = true;
    }

    return ev;
  }

  bool isDown() const { return s_down; }

  uint32_t holdMs(uint32_t nowMs) const {
    return s_down ? (uint32_t)(nowMs - s_downSince) : 0;
  }

  // Progress toward the long-press threshold, 0..1000 per mille, for the
  // on-screen hold indicator.
  uint16_t holdPermille(uint32_t nowMs) const {
    if (!s_down) return 0;
    uint32_t held = (uint32_t)(nowMs - s_downSince);
    if (held >= BTN_LONG_PRESS_MS) return 1000;
    return (uint16_t)(held * 1000 / BTN_LONG_PRESS_MS);
  }

 private:
  bool s_down = false;
  bool s_rawLast = false;
  bool s_longFired = false;
  uint32_t s_rawSince = 0;
  uint32_t s_downSince = 0;
};
