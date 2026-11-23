// The `/api/calendar/events` payload: the day's agenda, ready for the app.
//
// No `Arduino.h` here on purpose. This is the only interface between the device
// and the tray, so the exact bytes are pinned on the host by
// `tools/calendar_events_test.cpp` before the board produces them.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <ArduinoJson.h>

#include "calendar_model.h"

// Writes `{ ok, tz, dayStart, dayEnd, count, events[] }` into `doc`.
// `tzOffsetHours` is the device's own offset, the same one `/api/status` reports,
// so the app can format local hours without a second call; the event times
// themselves stay UTC epoch seconds because the device does not format text for
// anyone.
//
// `dayStart`/`dayEnd` are the local midnight of today and of tomorrow, computed
// from `nowEpoch`: the device knows its offset, the PC does not, so "hoy" and
// "mañana" are never the app's guess. An unsynced clock (before 2020) sends
// zeros, and the app falls back to `tz` instead of trusting a 1970 midnight.
//
// Strings are copied by pointer, so `events` must outlive `doc`'s serialization.
void calendarEventsJson(const CalendarEvent *events, size_t count,
                        int tzOffsetHours, int64_t nowEpoch, JsonDocument &doc);
