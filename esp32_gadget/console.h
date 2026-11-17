// Serial console: write policy and the interactive command line.
//
// The firmware runs with `setTxTimeoutMs(0)` so a closed serial port can never
// wedge it, but that silently discards bytes when a response is longer than the
// host drains. Anything longer than a short line has to switch to a blocking
// write for its duration, or it arrives truncated.
//
// The console itself is a first-class part of the firmware, not debug
// leftovers: the panel has no readable bus, so `dump` (an ASCII render of the
// framebuffer) and `press`/`hold` (synthetic gestures) are how the display and
// the game are verified without a finger on the board. The command
// implementations live in console.cpp so the sketch stays the boot and main
// loop.
#pragma once

#include <Arduino.h>

inline void txReliable(bool on) { Serial.setTxTimeoutMs(on ? 400 : 0); }

// Reads whatever the host typed and dispatches complete lines. Call every loop.
void consolePoll();

// True while a synthetic press from the console (`press`/`hold`) should keep
// the button pin logically down. Expires on its own so a lost host cannot leave
// the firmware holding a button forever.
bool consoleSyntheticPressActive(uint32_t nowMs);
