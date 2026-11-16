// Console write policy, shared by the sketch and the network module.
//
// The firmware runs with `setTxTimeoutMs(0)` so a closed serial port can never
// wedge it, but that silently discards bytes when a response is longer than the
// host drains. Anything longer than a short line has to switch to a blocking
// write for its duration, or it arrives truncated.
#pragma once

#include <Arduino.h>

inline void txReliable(bool on) { Serial.setTxTimeoutMs(on ? 400 : 0); }
