// What the device sends Google to create or change an event.
//
// Pure and header-free of `Arduino.h` on purpose: these are the exact bytes that
// leave the board when the owner edits from the tray, so they are pinned on the
// host by `tools/calendar_write_test.cpp` before a board produces them. The
// reading side lives in `calendar_parse.cpp`; this is its mirror.
#pragma once

#include <stdint.h>

#include <ArduinoJson.h>

// Fills `doc` with the body Google's `events.insert` and `events.patch` expect.
//
// A timed event carries `dateTime` with the owner's offset written out, not a
// bare `Z`: the instant is the same, but an event edited from the tray keeps the
// timezone it already had in the calendar. An all-day event carries `date`, and
// its end is the day *after* the last second it covers, because Google's all-day
// end is exclusive — the parse side reads exactly that back.
//
// Returns false when a timestamp cannot be written, so a bad request never
// reaches the network as a half-filled event.
bool calendarEventBodyJson(const char *summary, const char *location,
                           int64_t startEpoch, int64_t endEpoch, bool allDay,
                           int tzOffsetHours, JsonDocument &doc);
