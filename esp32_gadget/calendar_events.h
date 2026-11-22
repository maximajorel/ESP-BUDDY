// The `/api/calendar/events` payload: the day's agenda, ready for the app.
//
// No `Arduino.h` here on purpose. This is the only interface between the device
// and the tray, so the exact bytes are pinned on the host by
// `tools/calendar_events_test.cpp` before the board produces them.
#pragma once

#include <stddef.h>

#include <ArduinoJson.h>

#include "calendar_model.h"

// Writes `{ ok, tz, count, events[] }` into `doc`. `tzOffsetHours` is the
// device's own offset, the same one `/api/status` reports, so the app can format
// local hours without a second call; the event times themselves stay UTC epoch
// seconds because the device does not format text for anyone.
//
// Strings are copied by pointer, so `events` must outlive `doc`'s serialization.
void calendarEventsJson(const CalendarEvent *events, size_t count,
                        int tzOffsetHours, JsonDocument &doc);
