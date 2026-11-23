// The `/api/calendar/month` payload: one month of the grid, ready for the tray.
//
// No `Arduino.h` here on purpose. Like `calendar_events.h`, this is the only
// interface between the device and the app for the month, so the exact bytes are
// pinned on the host by `tools/calendar_month_test.cpp` before the board
// produces them. The two payloads are separate contracts: the day page is frozen
// for old clients, and this one carries `id` and `etag` because the tray edits
// from it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ArduinoJson.h>

#include "calendar_model.h"

// The month that was asked for, plus the window its grid covers. `monthStart` and
// `monthEnd` delimit the month itself; `gridStart` and `gridEnd` delimit the
// whole weeks the tray draws, which start before and end after the month. Both
// pairs are local midnights in the device's offset, so the app never derives one
// from its own clock.
struct CalendarMonthInfo {
  int year;
  int month;
  int tzOffsetHours;
  int64_t monthStart;
  int64_t monthEnd;
  int64_t gridStart;
  int64_t gridEnd;
  // Google answered with at least as many events as the board can hold, so some
  // of the month is missing. The tray says so instead of showing a full-looking
  // month that is secretly cut.
  bool truncated;
};

// Writes `{ ok, tz, year, month, monthStart, monthEnd, gridStart, gridEnd,
// count, truncated, events[] }` into `doc`. Event times are UTC epoch seconds
// for the same reason as the day page: the device does not format text for
// anyone. Strings are copied by pointer, so `events` must outlive the
// serialization.
void calendarMonthJson(const CalendarMonthEvent *events, size_t count,
                       const CalendarMonthInfo &info, JsonDocument &doc);
