// Google Calendar's JSON, turned into fixed structs.
//
// Pure enough for the host: ArduinoJson is the only dependency, no Arduino.h
// and no network. Google owns the wire format, so everything here is defensive:
// a missing field, an unknown status or a timestamp that does not parse drops
// the entry instead of inventing one.
#pragma once

#include <stddef.h>

#include <ArduinoJson.h>

#include "calendar_model.h"

// Reads `{items: [{id, summary, primary, defaultReminders}]}`. Returns how many
// were written, capped by `cap`. The primary calendar is not reordered here: the
// panel puts it first because that is a presentation rule.
size_t calendarParseCalendarList(const char *json, CalendarInfo *out, size_t cap);

// Reads `{items: [{summary, status, start, end, reminders}]}`.
//
// `defaultLeadMin` is the selected calendar's default, already resolved by the
// caller (calendar default, then the panel's fallback). An event that uses the
// defaults takes it; an event with its own overrides takes the earliest one; an
// event whose reminder list is explicitly empty gets -1 and never reacts.
//
// `tzOffsetHours` is the owner's fixed offset, used only for all-day events,
// which carry a date instead of a timestamp.
size_t calendarParseEvents(const char *json, CalendarEvent *out, size_t cap,
                           int defaultLeadMin, int tzOffsetHours);

// Reads `{items: [{id, etag, status, summary, location, start, end}]}` for the
// month grid.
//
// Differs from `calendarParseEvents` on purpose: an entry without an `id` is
// dropped, because the whole point of this payload is that the owner can edit
// what he sees and an event nobody can address is not editable. `allocator` is
// the memory the JSON tree is built in — PSRAM on the board, the default heap on
// the host — and may be null for the default.
size_t calendarParseMonthEvents(const char *json, CalendarMonthEvent *out,
                                size_t cap, int tzOffsetHours,
                                ArduinoJson::Allocator *allocator);
